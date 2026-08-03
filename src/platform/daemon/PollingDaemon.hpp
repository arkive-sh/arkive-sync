#pragma once

#include "platform/Daemon.hpp"
#include "platform/DaemonServices.hpp"

class PollingDaemon final : public Daemon {
public:
  explicit PollingDaemon(DaemonServices services);
  ~PollingDaemon() override;

  int run() override;

private:
  DaemonServices services_;
};
