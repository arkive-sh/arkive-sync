#pragma once

struct SyncRoot;

class EntryRepo;

class SyncReconciler {
public:
  explicit SyncReconciler(EntryRepo &entryRepo);

  void reconcileRoot(const SyncRoot &root);

private:
  EntryRepo &entryRepo_;
};
