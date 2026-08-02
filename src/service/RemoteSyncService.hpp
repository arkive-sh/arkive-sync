#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SyncRoot;

class RemoteScanner;
class RemoteEntryRepo;
class SyncRepo;

class RemoteSyncService {
public:
  RemoteSyncService(std::unique_ptr<RemoteScanner> remoteScanner,
                    RemoteEntryRepo &remoteEntryRepo, SyncRepo &syncRepo);
  ~RemoteSyncService();

  bool runTick(const std::vector<SyncRoot> &roots);

private:
  std::unique_ptr<RemoteScanner> remoteScanner_;
  RemoteEntryRepo &remoteEntryRepo_;
  SyncRepo &syncRepo_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      lastRemoteScanAt_;
};
