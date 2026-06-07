#pragma once

#include "repo/LocalEntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/ScanSession.hpp"
#include "repo/SyncRootRepo.hpp"

#include <sqlite3.h>

class LocalPathProtector;

class SyncRepo {
public:
  SyncRepo(sqlite3 *db, LocalPathProtector &pathProtector);

  SyncRootRepo &roots() { return roots_; }
  const SyncRootRepo &roots() const { return roots_; }
  LocalEntryRepo &local() { return local_; }
  const LocalEntryRepo &local() const { return local_; }
  RemoteEntryRepo &remote() { return remote_; }
  const RemoteEntryRepo &remote() const { return remote_; }

  SyncScanSession beginScan() const;
  void releaseMemory() const;

private:
  sqlite3 *db_;
  SyncRootRepo roots_;
  LocalEntryRepo local_;
  RemoteEntryRepo remote_;
};
