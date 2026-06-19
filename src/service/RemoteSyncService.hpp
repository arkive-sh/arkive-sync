#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SyncRoot;

class EntryRepo;
class RemoteScanner;

class RemoteSyncService {
public:
  explicit RemoteSyncService(std::unique_ptr<RemoteScanner> remoteScanner);
  ~RemoteSyncService();

  void runTick(const std::vector<SyncRoot> &roots);

private:
  std::unique_ptr<RemoteScanner> remoteScanner_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      lastRemoteScanAt_;
};
