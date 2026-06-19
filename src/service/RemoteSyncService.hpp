#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

struct SyncRoot;

class EntryRepo;
class RemoteScanner;

class RemoteSyncService {
public:
  RemoteSyncService(EntryRepo &entryRepo, RemoteScanner *remoteScanner);

  void runTick(const std::vector<SyncRoot> &roots);

private:
  void reconcileDeletedEntries(const SyncRoot &root);

  EntryRepo &entryRepo_;
  RemoteScanner *remoteScanner_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      lastRemoteScanAt_;
};
