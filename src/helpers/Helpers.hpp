#pragma once

#include <memory>
#include <termios.h>

struct TerminalState {
  termios original{};
};

struct TerminalEchoRestore {
  void operator()(TerminalState *state) const noexcept;
};

using TerminalEchoGuard = std::unique_ptr<TerminalState, TerminalEchoRestore>;

TerminalEchoGuard makeTerminalEchoGuard();
