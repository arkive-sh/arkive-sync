#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct ListSyncEntriesResponse;
struct SyncEntryResponse;

class ArkiveApi;
class EntryRepo;
class RemoteEntryRepo;
class UserRepo;
class SyncRepo;
class RustCrypto;
class VaultService;

class RemoteScanner {
public:
  RemoteScanner(SyncRepo &syncRepo, EntryRepo &entryRepo,
                RemoteEntryRepo &remoteEntryRepo, ArkiveApi &api,
                RustCrypto &crypto, VaultService &vaultService,
                UserRepo &userRepo);

  void scanRoot(const std::string &syncRootId) const;
  bool isRootDeleted(const std::string &syncRootId) const;
  ListSyncEntriesResponse
  fetchEntries(const std::optional<std::string> &folderId) const;

private:
  std::optional<std::string> decryptEntryName(const SyncEntryResponse &entry) const;
  void scanFolder(const std::string &syncRootId,
                  const std::optional<std::string> &remoteFolderId,
                  const std::filesystem::path &localParentPath) const;

  SyncRepo &syncRepo_;
  EntryRepo &entryRepo_;
  RemoteEntryRepo &remoteEntryRepo_;
  ArkiveApi &api_;
  RustCrypto &crypto_;
  VaultService &vaultService_;
  UserRepo &userRepo_;
};
