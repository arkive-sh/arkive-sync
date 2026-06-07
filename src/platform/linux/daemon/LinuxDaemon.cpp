#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "fs/FileWatcher.hpp"
#include "service/SyncScheduler.hpp"
#include "sync/QueueWorker.hpp"
#include "sync/RemoteScanner.hpp"
#include "sync/RemoteSyncWorker.hpp"
#include "sync/ScanWorker.hpp"

#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "helpers/LocalPathProtector.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UserRepo.hpp"
#include "service/VaultService.hpp"

#include <cerrno>
#include <csignal>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <system_error>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleStopSignal(int) { gStopRequested = 1; }

class ScopedFd {
public:
  explicit ScopedFd(int fd) : fd_(fd) {}

  ~ScopedFd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;

  int get() const { return fd_; }

private:
  int fd_{-1};
};

class ScopedSignalHandlers {
public:
  ScopedSignalHandlers()
      : previousSigint_(std::signal(SIGINT, handleStopSignal)),
        previousSigterm_(std::signal(SIGTERM, handleStopSignal)) {}

  ~ScopedSignalHandlers() {
    std::signal(SIGINT, previousSigint_);
    std::signal(SIGTERM, previousSigterm_);
  }

  ScopedSignalHandlers(const ScopedSignalHandlers &) = delete;
  ScopedSignalHandlers &operator=(const ScopedSignalHandlers &) = delete;

private:
  using SignalHandler = void (*)(int);

  SignalHandler previousSigint_;
  SignalHandler previousSigterm_;
};

struct RemoteWorkerContext {
  Database db;
  UserRepo userRepo;
  RustCrypto crypto;
  VaultService vaultService;
  LocalPathProtector pathProtector;
  SyncRepo syncRepo;
  ArkiveHttpClient client;
  ArkiveApi api;
  RemoteScanner scanner;
  RemoteSyncWorker worker;

  RemoteWorkerContext()
      : db(databasePath()), userRepo(db.getDb()), crypto(),
        vaultService(userRepo, crypto), pathProtector(crypto, vaultService),
        syncRepo(db.getDb(), pathProtector),
        client(loadBaseUrl(userRepo), cookieJarPath().string()), api(client),
        scanner(syncRepo, api), worker(scanner) {}

  static std::string loadBaseUrl(UserRepo &userRepo) {
    const auto account = userRepo.getAccount();
    if (!account.has_value() || account->baseUrl.empty()) {
      throw std::runtime_error("Base URL is missing");
    }
    return account->baseUrl;
  }
};

} // namespace

LinuxDaemon::LinuxDaemon(SyncScheduler &syncScheduler,
                         std::unique_ptr<IFileWatcher> watcher)
    : syncScheduler_(syncScheduler), watcher_(std::move(watcher)),
      queueWorker_(std::make_unique<QueueWorker>()),
      scanWorker_(
          std::make_unique<ScanWorker>([this] { queueWorker_->trigger(); })) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
  RemoteWorkerContext remoteWorkerContext;
  queueWorker_->setOnUploadsCompleted(
      [&remoteWorkerContext] { remoteWorkerContext.worker.trigger(); });
  remoteWorkerContext.worker.start();

  const std::vector<WatchRoot> watchRoots = syncScheduler_.rootsToWatch();
  if (watchRoots.empty()) {
    remoteWorkerContext.worker.stop();
    throw std::runtime_error("No enabled sync paths configured.");
  }

  for (const auto &root : watchRoots) {
    watcher_->addRoot(root);
  }
  syncScheduler_.enqueueFullRescan();
  queueWorker_->trigger();

  const ScopedFd epollFd(epoll_create1(EPOLL_CLOEXEC));
  if (epollFd.get() < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_create1 failed");
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = watcher_->fd();

  if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, watcher_->fd(), &event) < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl add watcher failed");
  }

  spdlog::info("Daemon watching {} sync root(s)", watchRoots.size());

  while (!gStopRequested) {
    const int timeoutMs = syncScheduler_.nextRunDelayMs();
    epoll_event readyEvents[8]{};
    const int readyCount =
        epoll_wait(epollFd.get(), readyEvents, 8, timeoutMs);

    if (readyCount < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::generic_category(),
                              "epoll_wait failed");
    }

    for (int i = 0; i < readyCount; ++i) {
      if (readyEvents[i].data.fd != watcher_->fd()) {
        continue;
      }

      for (const auto &fileEvent : watcher_->poll()) {
        syncScheduler_.enqueueEvent(fileEvent);
      }
    }

    const auto jobs = syncScheduler_.drainDueJobs();
    if (!jobs.empty()) {
      spdlog::info("Daemon enqueuing {} scan job(s)", jobs.size());
      for (auto &job : jobs) {
        scanWorker_->enqueue(std::move(job));
      }
    }
  }

  watcher_->stop();
  remoteWorkerContext.worker.stop();
  scanWorker_->stop();
  queueWorker_->stop();
  spdlog::info("Daemon stopped");
  return 0;
}
