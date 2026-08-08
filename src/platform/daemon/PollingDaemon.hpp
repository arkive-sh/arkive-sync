#pragma once

#include "platform/Daemon.hpp"
#include "platform/DaemonServices.hpp"

#include <functional>

class PollingDaemon final : public Daemon {
public:
  explicit PollingDaemon(
      DaemonServices services,
      std::function<void()> waitForEvents = nullptr);
  ~PollingDaemon() override;

  int run() override;

private:
  DaemonServices services_;
  std::function<void()> waitForEvents_;
};
