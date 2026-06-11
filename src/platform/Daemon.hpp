#pragma once

#include <memory>

class Daemon {
public:
  virtual ~Daemon() = default;

  Daemon() = default;
  Daemon(const Daemon &) = delete;
  Daemon &operator=(const Daemon &) = delete;
  Daemon(Daemon &&) = delete;
  Daemon &operator=(Daemon &&) = delete;

  virtual int run() = 0;

  static std::unique_ptr<Daemon> create();
};
