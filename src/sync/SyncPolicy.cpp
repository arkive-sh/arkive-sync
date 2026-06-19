#include "sync/SyncPolicy.hpp"

namespace {

SyncDecision decideUploadOnly(const SyncEntryState &state) {
  if (state.hasConflict) {
    return SyncDecision::Conflict;
  }
  if (state.localDeleted && state.remoteDirty) {
    return SyncDecision::Conflict;
  }
  if (state.localDeleted && state.remoteExists) {
    return SyncDecision::DeleteRemote;
  }
  if (state.localExists && state.localDirty) {
    return SyncDecision::Upload;
  }
  return SyncDecision::Noop;
}

SyncDecision decideRemoteMirror(const SyncEntryState &state) {
  if (state.hasConflict) {
    return SyncDecision::Conflict;
  }
  if (state.remoteDeleted && state.localDirty) {
    return SyncDecision::Conflict;
  }
  if (state.remoteDeleted && state.localExists) {
    return SyncDecision::DeleteLocal;
  }
  if (state.remoteExists && state.remoteDirty) {
    return SyncDecision::Download;
  }
  return SyncDecision::Noop;
}

SyncDecision decideTwoWay(const SyncEntryState &state) {
  if (state.hasConflict) {
    return SyncDecision::Conflict;
  }
  if ((state.localDeleted && state.remoteDirty) ||
      (state.remoteDeleted && state.localDirty) ||
      (state.localDirty && state.remoteDirty)) {
    return SyncDecision::Conflict;
  }
  if (state.localDeleted && state.remoteExists) {
    return SyncDecision::DeleteRemote;
  }
  if (state.remoteDeleted && state.localExists) {
    return SyncDecision::DeleteLocal;
  }
  if (state.localExists && state.localDirty) {
    return SyncDecision::Upload;
  }
  if (state.remoteExists && state.remoteDirty) {
    return SyncDecision::Download;
  }
  return SyncDecision::Noop;
}

} // namespace

const char *toSyncDecisionName(SyncDecision decision) {
  switch (decision) {
  case SyncDecision::Noop:
    return "noop";
  case SyncDecision::Upload:
    return "upload";
  case SyncDecision::Download:
    return "download";
  case SyncDecision::DeleteLocal:
    return "delete_local";
  case SyncDecision::DeleteRemote:
    return "delete_remote";
  case SyncDecision::Conflict:
    return "conflict";
  }

  return "noop";
}

SyncDecision SyncPolicy::decide(const SyncEntryState &state, SyncMode mode) {
  switch (mode) {
  case SyncMode::UploadOnly:
    return decideUploadOnly(state);
  case SyncMode::RemoteMirror:
    return decideRemoteMirror(state);
  case SyncMode::TwoWay:
    return decideTwoWay(state);
  }

  return SyncDecision::Noop;
}
