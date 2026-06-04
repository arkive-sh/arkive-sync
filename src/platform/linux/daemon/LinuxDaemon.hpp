#pragma once

#include "platform/Daemon.hpp"

class SyncScheduler;

class LinuxDaemon final : public Daemon {
public:
  explicit LinuxDaemon(SyncScheduler &syncScheduler);

  int run() override;

private:
  SyncScheduler &syncScheduler_;
};
