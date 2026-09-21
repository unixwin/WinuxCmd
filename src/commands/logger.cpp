// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Windows-native logger command.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LOGGER_OPTIONS = std::array{
    // [GNU] -i, --id: log the logger command's PID
    OPTION("-i", "--id", "log the logger command's PID"),
    // [GNU] -f, --file: log the contents of this file
    OPTION("-f", "--file", "log the contents of this file", STRING_TYPE),
    // [GNU] -e, --skip-empty: do not log empty lines when processing files
    OPTION("-e", "--skip-empty",
           "do not log empty lines when processing files"),
    // [GNU] -p, --priority: mark given message with this priority
    OPTION("-p", "--priority", "message priority", STRING_TYPE),
    // [GNU] -s, --stderr: output message to standard error as well
    OPTION("-s", "--stderr", "also write the message to standard error"),
    // [GNU] -t, --tag: mark every line with this tag
    OPTION("-t", "--tag", "mark every line with this tag", STRING_TYPE),
    // [GNU] -n, --server: write to this remote syslog server
    OPTION("-n", "--server", "write to this remote syslog server", STRING_TYPE),
    // [GNU] -P, --port: use this port for UDP or TCP connection
    OPTION("-P", "--port", "use this port for UDP or TCP connection",
           STRING_TYPE),
    // [GNU] -T, --tcp: use TCP only
    OPTION("-T", "--tcp", "use TCP only"),
    // [GNU] -d, --udp: use UDP only
    OPTION("-d", "--udp", "use UDP only"),
    // [GNU] --prio-prefix: look for a prefix on every line read from stdin
    OPTION("", "--prio-prefix",
           "look for a prefix on every line read from stdin"),
    // [GNU] --no-act: do everything except the write the log
    OPTION("", "--no-act", "do everything except the write the log"),
    // [GNU] -S, --size: maximum size for a single message
    OPTION("-S", "--size", "maximum size for a single message", STRING_TYPE),
    // [GNU] --rfc3164: use the obsolete BSD syslog protocol
    OPTION("", "--rfc3164", "use the obsolete BSD syslog protocol"),
    // [GNU] --rfc5424: use the syslog protocol (default for remote)
    OPTION("", "--rfc5424", "use the syslog protocol (default for remote)")};

namespace logger_pipeline {

struct Config {
  std::string priority = "user.notice";
  std::string tag = "logger";
  bool stderr_too = false;
  std::string message;
};

auto event_type_for_priority(std::string_view priority) -> WORD {
  std::string lower = ascii_lower_copy(priority);
  if (lower.find("emerg") != std::string::npos ||
      lower.find("alert") != std::string::npos ||
      lower.find("crit") != std::string::npos ||
      lower.find("err") != std::string::npos) {
    return EVENTLOG_ERROR_TYPE;
  }
  if (lower.find("warning") != std::string::npos ||
      lower.find("warn") != std::string::npos) {
    return EVENTLOG_WARNING_TYPE;
  }
  return EVENTLOG_INFORMATION_TYPE;
}

auto join_message(std::span<const std::string_view> args) -> std::string {
  std::string out;
  for (auto arg : args) {
    if (!out.empty()) out.push_back(' ');
    out.append(arg);
  }
  return out;
}

auto read_stdin_message() -> std::string {
  std::ostringstream out;
  out << std::cin.rdbuf();
  return out.str();
}

auto write_event_log(const Config& cfg) -> bool {
  HANDLE raw = RegisterEventSourceW(nullptr, L"WinuxCmd");
  if (raw == nullptr) return false;
  UniqueHandle source(raw);

  std::wstring text = utf8_to_wstring(cfg.tag + ": " + cfg.message);
  const wchar_t* strings[] = {text.c_str()};
  BOOL ok = ReportEventW(source.get(), event_type_for_priority(cfg.priority), 0,
                         0, nullptr, 1, 0, strings, nullptr);
  DeregisterEventSource(source.release());
  return ok != FALSE;
}

auto run(const CommandContext<LOGGER_OPTIONS.size()>& ctx) -> int {
  Config cfg;
  cfg.priority = ctx.get<std::string>(
      "-p", ctx.get<std::string>("--priority", cfg.priority));
  cfg.tag = ctx.get<std::string>("-t", ctx.get<std::string>("--tag", cfg.tag));
  cfg.stderr_too =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--stderr", false);
  // --id: accepted for compatibility, no-op on Windows
  (void)ctx.has("--id");
  cfg.message = join_message(ctx.positionals);
  if (cfg.message.empty()) cfg.message = read_stdin_message();

  if (cfg.message.empty()) return 0;

  const std::string rendered = cfg.tag + ": " + cfg.message;
  OutputDebugStringW(utf8_to_wstring(rendered).c_str());
  (void)write_event_log(cfg);
  if (cfg.stderr_too) safeErrorPrintLn(rendered);
  return 0;
}

}  // namespace logger_pipeline

REGISTER_COMMAND(logger, "logger", "logger [OPTION]... [MESSAGE]",
                 "Write messages to the Windows event/logging facilities.",
                 "  logger -t build finished\n"
                 "  echo finished | logger -s",
                 "eventcreate(1), regtool(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", LOGGER_OPTIONS) {
  return logger_pipeline::run(ctx);
}
