#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct ListSyncEntriesResponse;

class ArkiveApi;
class EntryRepo;
class SyncRepo;
class RustCrypto;
class VaultService;

class RemoteScanner {
public:
  RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo, ArkiveApi &api,
                RustCrypto &crypto, VaultService &vaultService);

  void scanRoot(const std::string &syncRootId) const;
  bool isRootDeleted(const std::string &syncRootId) const;
  ListSyncEntriesResponse
  fetchEntries(const std::optional<std::string> &folderId) const;

private:
  std::optional<std::string>
  decryptEntryName(const std::optional<std::string> &encryptedName) const;
  void scanFolder(const std::string &syncRootId,
                  const std::optional<std::string> &remoteFolderId,
                  const std::filesystem::path &localParentPath) const;

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  ArkiveApi &api_;
  RustCrypto &crypto_;
  VaultService &vaultService_;
};
