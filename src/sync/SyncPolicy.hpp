#pragma once

#include "repo/EntryRepo.hpp"
#include "sync/SyncMode.hpp"

enum class SyncDecision {
  Noop,
  Upload,
  Download,
  DeleteLocal,
  DeleteRemote,
  Conflict,
};

class SyncPolicy {
public:
  static SyncDecision decide(const SyncEntryState &state, SyncMode mode);
};
