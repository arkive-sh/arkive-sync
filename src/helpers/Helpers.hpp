#pragma once

#include "repo/UserRepo.hpp"
#include <memory>
#include <string>
#include <termios.h>

struct TerminalState {
  termios original{};
};

struct TerminalEchoRestore {
  void operator()(TerminalState *state) const noexcept;
};

using TerminalEchoGuard = std::unique_ptr<TerminalState, TerminalEchoRestore>;

TerminalEchoGuard makeTerminalEchoGuard();
std::string readPasswordFromTerminal(const std::string &prompt);
bool commandAllowsMissingBaseUrl(int argc, char *argv[]);
std::string requireBaseUrl(const UserRepo &userRepo);
