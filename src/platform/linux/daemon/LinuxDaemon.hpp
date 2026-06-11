#pragma once

#include "platform/Daemon.hpp"

#include <memory>

class IFileWatcher;

class LinuxDaemon final : public Daemon {
public:
  explicit LinuxDaemon(std::unique_ptr<IFileWatcher> watcher);
  ~LinuxDaemon() override;

  int run() override;

private:
  std::unique_ptr<IFileWatcher> watcher_;
};
