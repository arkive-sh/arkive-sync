#pragma once

#include "api/ArkiveApi.hpp"

class ArkiveApi;
class SyncRepo;

class RemoteScanner {
public:
  RemoteScanner(SyncRepo &syncRepo, ArkiveApi &api);

  void scanAllRootsAndStore(bool includeDeleted = true) const;
  void scanFolderAndStore(const std::string &syncRootId,
                          const std::optional<std::string> &folderId,
                          bool includeDeleted = true) const;

private:
  static constexpr size_t kPageLimit = 100;

  SyncRepo &syncRepo_;
  ArkiveApi &api_;
};
