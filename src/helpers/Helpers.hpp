#pragma once

#include "repo/UserRepo.hpp"
#include <memory>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <termios.h>
#endif

struct TerminalState {
#if defined(_WIN32)
  HANDLE input{INVALID_HANDLE_VALUE};
  DWORD originalMode{0};
#else
  termios original{};
#endif
};

struct TerminalEchoRestore {
  void operator()(TerminalState *state) const noexcept;
};

using TerminalEchoGuard = std::unique_ptr<TerminalState, TerminalEchoRestore>;

TerminalEchoGuard makeTerminalEchoGuard();
std::string readPasswordFromTerminal(const std::string &prompt);
bool commandAllowsMissingBaseUrl(int argc, char *argv[]);
std::string requireBaseUrl(const UserRepo &userRepo);
std::string getCurrentTimestamp();
