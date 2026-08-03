#include "platform/macos/watcher/FSEventsWatcher.hpp"

#include <stdexcept>

namespace {

void fseventsCallback(ConstFSEventStreamRef, void *context, size_t,
                      void *, const FSEventStreamEventFlags[], const FSEventStreamEventId[]) {
  auto *root = static_cast<FSEventsWatcher::RootState *>(context);
  root->dirty = true;
}

} // namespace

FSEventsWatcher::~FSEventsWatcher() { stop(); }

void FSEventsWatcher::addRoot(const WatchRoot &root) {
  auto state = std::make_unique<RootState>();
  state->rootId = root.rootId;
  state->path = root.path;
  state->snapshot = takeFileSnapshot(root.path);

  CFStringRef path = CFStringCreateWithFileSystemRepresentation(
      kCFAllocatorDefault, root.path.c_str());
  if (path == nullptr) {
    throw std::runtime_error("FSEvents path allocation failed");
  }
  CFArrayRef paths =
      CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(&path),
                    1, &kCFTypeArrayCallBacks);
  CFRelease(path);
  if (paths == nullptr) {
    throw std::runtime_error("FSEvents paths allocation failed");
  }

  FSEventStreamContext context{};
  context.info = state.get();
  state->stream = FSEventStreamCreate(
      kCFAllocatorDefault, fseventsCallback, &context, paths,
      kFSEventStreamEventIdSinceNow, 0.1,
      kFSEventStreamCreateFlagFileEvents |
          kFSEventStreamCreateFlagNoDefer);
  CFRelease(paths);
  if (state->stream == nullptr) {
    throw std::runtime_error("FSEventStreamCreate failed");
  }

  state->thread = std::thread([rootState = state.get()] {
    rootState->runLoop = CFRunLoopGetCurrent();
    FSEventStreamScheduleWithRunLoop(rootState->stream, rootState->runLoop,
                                     kCFRunLoopDefaultMode);
    FSEventStreamStart(rootState->stream);
    CFRunLoopRun();
    FSEventStreamStop(rootState->stream);
    FSEventStreamInvalidate(rootState->stream);
  });

  std::lock_guard lock(mutex_);
  roots_.push_back(std::move(state));
}

std::vector<FileEvent> FSEventsWatcher::poll() {
  std::vector<FileEvent> events;
  std::lock_guard lock(mutex_);

  for (auto &root : roots_) {
    if (!root->dirty.exchange(false)) {
      continue;
    }
    auto next = takeFileSnapshot(root->path);
    auto diff = diffFileSnapshots(root->rootId, root->snapshot, next);
    root->snapshot = std::move(next);
    events.insert(events.end(), std::make_move_iterator(diff.begin()),
                  std::make_move_iterator(diff.end()));
  }

  return events;
}

void FSEventsWatcher::stop() {
  std::vector<std::unique_ptr<RootState>> roots;
  {
    std::lock_guard lock(mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
    roots.swap(roots_);
  }

  for (auto &root : roots) {
    if (root->runLoop != nullptr) {
      CFRunLoopStop(root->runLoop);
    }
    if (root->thread.joinable()) {
      root->thread.join();
    }
    if (root->stream != nullptr) {
      FSEventStreamRelease(root->stream);
    }
  }
}
