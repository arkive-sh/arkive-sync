#pragma once

#include <optional>
#include <string>

struct ListSyncEntriesResponse;

class ArkiveApi;
class EntryRepo;
class SyncRepo;

class RemoteScanner {
public:
  RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo, ArkiveApi &api);

  void scanRoot(const std::string &syncRootId) const;
  bool isRootDeleted(const std::string &syncRootId) const;
  ListSyncEntriesResponse
  fetchEntries(const std::optional<std::string> &folderId) const;

private:
  void scanFolder(const std::string &syncRootId,
                  const std::optional<std::string> &remoteFolderId) const;

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  ArkiveApi &api_;
};
