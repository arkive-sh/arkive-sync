#pragma once

#include "api/ArkiveApi.hpp"

#include <cstddef>
#include <vector>

class ArkiveApi;
class SyncRepo;

struct RemoteScanRootResult {
  std::string syncRootId;
  std::optional<std::string> remoteFolderId;
  ListSyncEntriesResponse response;
};

struct RemoteScanResult {
  std::vector<RemoteScanRootResult> roots;

  size_t totalEntryCount() const;
};

class RemoteScanner {
public:
  RemoteScanner(SyncRepo &syncRepo, ArkiveApi &api);

  RemoteScanResult scanAllRoots(bool includeDeleted = true) const;
  ListSyncEntriesResponse
  scanFolder(const std::optional<std::string> &folderId,
             bool includeDeleted = true) const;

private:
  SyncRepo &syncRepo_;
  ArkiveApi &api_;
};
