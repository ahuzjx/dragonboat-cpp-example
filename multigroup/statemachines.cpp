// Copyright 2019 JasonYuchen (jasonyuchen@foxmail.com)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <cstring>
#include <sstream>
#include <cassert>
#include <algorithm>
#include "statemachines.h"
#include "utils.h"

void KVStoreStateMachine::update(dragonboat::Entry &ent) noexcept
{
  std::string query(reinterpret_cast<const char *>(ent.cmd), ent.cmdLen);
  auto parts = split(query);
  if (parts[0] == "set") {
    kvstore_[parts[1]] = parts[2];
  } else if (parts[0] == "del") {
    kvstore_.erase(parts[1]);
  } else if (parts[0] == "clr") {
    kvstore_.clear();
  } else {
    ent.result = update_count_;
  }
  update_count_++;
  ent.result = update_count_;
}

LookupResult KVStoreStateMachine::lookup(
  const dragonboat::Byte *data,
  size_t size) const noexcept
{
  LookupResult r;
  std::string query(reinterpret_cast<const char *>(data), size);
  std::string result;
  if (query == "display") {
    std::stringstream ss;
    ss << "{ ";
    std::for_each(
      kvstore_.cbegin(),
      kvstore_.cend(),
      [&ss](const std::pair<const std::string, std::string> &item)
      {
        ss << "\"" << item.first << "\":\"" << item.second << "\", ";
      });
    ss << "}";
    auto str = ss.str();
    r.result = new char[str.size()];
    r.size = str.size();
    std::memcpy(r.result, str.data(), r.size);
    return r;
  }
  auto it = kvstore_.find(query);
  if (it == kvstore_.end()) {
    char nf[] = "not found";
    r.result = new char[sizeof(nf)];
    r.size = sizeof(nf);
    std::memcpy(r.result, nf, r.size);
  } else {
    r.result = new char[it->second.size()];
    r.size = it->second.size();
    std::memcpy(r.result, it->second.data(), r.size);
  }
  return r;
}

uint64_t KVStoreStateMachine::getHash() const noexcept
{
  return static_cast<uint64_t>(update_count_);
}

SnapshotResult KVStoreStateMachine::saveSnapshot(
  dragonboat::SnapshotWriter *writer,
  dragonboat::SnapshotFileCollection *collection,
  const dragonboat::DoneChan &done) const noexcept
{
  SnapshotResult r;
  dragonboat::IOResult ret;
  r.errcode = SNAPSHOT_OK;
  r.size = 0;
  std::string ss;
  ss.append(std::to_string(update_count_)).append("\n");
  std::for_each(
    kvstore_.cbegin(),
    kvstore_.cend(),
    [&ss](const std::pair<const std::string, std::string> &kv)
    {
      ss.append(kv.first).append(" ").append(kv.second).append("\n");
    });
  if (done.Closed()) {
    r.errcode = SNAPSHOT_STOPPED;
    return r;
  } else {
    ret = writer->Write(
      reinterpret_cast<const dragonboat::Byte *>(ss.data()),
      ss.size());
    if (ret.size != ss.size()) {
      r.errcode = FAILED_TO_SAVE_SNAPSHOT;
      return r;
    }
  }
  r.size = ss.size();
  return r;
}

int KVStoreStateMachine::recoverFromSnapshot(
  dragonboat::SnapshotReader *reader,
  const std::vector<dragonboat::SnapshotFile> &files,
  const dragonboat::DoneChan &done) noexcept
{
  assert(kvstore_.empty());
  assert(update_count_ == 0);
  constexpr size_t BUF_SIZE = 4096;
  dragonboat::IOResult ret;
  dragonboat::Byte data[BUF_SIZE];
  std::stringstream ss;
  while (true) {
    ret = reader->Read(data, BUF_SIZE);
    if (ret.size <= 0) {
      break;
    }
    ss.write(reinterpret_cast<const char *>(data), ret.size);
  }
  if (ret.size < 0) {
    return FAILED_TO_RECOVER_FROM_SNAPSHOT;
  } else if (ret.size == 0) {
    std::string count;
    ss >> count;
    update_count_ = std::stoi(count);
    std::string key;
    std::string val;
    while (ss >> key >> val) {
      kvstore_[key] = val;
    }
  }
  return SNAPSHOT_OK;
}

void KVStoreStateMachine::freeLookupResult(LookupResult r) noexcept
{
  delete[] r.result;
}

dragonboat::RegularStateMachine *createDragonboatStateMachine(
  uint64_t clusterID,
  uint64_t nodeID)
{
  return new KVStoreStateMachine(clusterID, nodeID);
}