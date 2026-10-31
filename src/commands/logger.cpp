// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Windows-native logger command.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

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
    OPTION("", "--rfc5424", "use the syslog protocol (default for remote)"),
    // [GNU] --msgid/--sd-id/--sd-param fill the RFC 5424 header fields
    // (logger.c syslog_rfc5424_header); --octet-counting switches TCP
    // framing to RFC 6587 octet counting (logger.c:448-457).
    OPTION("", "--msgid", "set the RFC 5424 message id", STRING_TYPE),
    OPTION("", "--sd-id", "add structured data with this id", STRING_TYPE),
    OPTION("", "--sd-param", "add a parameter to the last structured data id",
           STRING_TYPE),
    OPTION("", "--octet-counting", "use RFC 6587 octet counting for TCP"),
    // [GNU] -u/--socket and --journald address Unix syslog facilities
    // with no Windows counterpart; declared and explicitly refused.
    OPTION("-u", "--socket", "write to this Unix socket", STRING_TYPE),
    OPTION("", "--journald", "write to the systemd journal")};

namespace logger_pipeline {

struct Config {
  int priority = 13;  // user.notice
  std::string tag;
  bool stderr_too = false;
  bool include_pid = false;
  bool skip_empty = false;
  bool no_act = false;
  bool prio_prefix = false;
  bool rfc5424 = false;
  bool rfc3164 = false;
  bool octet_count = false;
  bool use_tcp = false;
  std::string msgid;
  std::string structured_data;  // prebuilt "[id param ...]" sequence
  std::string file;
  std::string server;
  std::string port = "514";
  // [GNU] the message size defaults to 1024 (logger.c:1121); -S
  // overrides it, and 0 disables truncation.
  size_t max_size = 1024;
};

// [GNU] util-linux logger.c parses the -p argument against the syslog
// facility/severity name tables (logger.c:230,236) and dies on unknown
// names: "unknown facility name: %s" / "unknown priority name: %s".
// Accepted forms mirror the oracle: "facility.severity", "severity",
// or a bare numeric priority value.
auto facility_value(std::string_view name) -> std::optional<int> {
  static constexpr std::array<std::pair<std::string_view, int>, 24> table = {{
      {"kern", 0},   {"user", 1},    {"mail", 2},   {"daemon", 3},
      {"auth", 4},   {"syslog", 5},  {"lpr", 6},    {"news", 7},
      {"uucp", 8},   {"cron", 9},    {"authpriv", 10}, {"ftp", 11},
      {"local0", 16}, {"local1", 17}, {"local2", 18}, {"local3", 19},
      {"local4", 20}, {"local5", 21}, {"local6", 22}, {"local7", 23},
  }};
  for (const auto& [n, v] : table) {
    if (n == name) return v;
  }
  return std::nullopt;
}

auto severity_value(std::string_view name) -> std::optional<int> {
  static constexpr std::array<std::pair<std::string_view, int>, 11> table = {{
      {"emerg", 0}, {"panic", 0}, {"alert", 1}, {"crit", 2}, {"err", 3},
      {"error", 3}, {"warning", 4}, {"warn", 4}, {"notice", 5}, {"info", 6},
      {"debug", 7},
  }};
  for (const auto& [n, v] : table) {
    if (n == name) return v;
  }
  return std::nullopt;
}

auto parse_small_int(std::string_view text, int max_value) -> std::optional<int> {
  if (text.empty()) return std::nullopt;
  int value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size() || value < 0 ||
      value > max_value) {
    return std::nullopt;
  }
  return value;
}

// On error, `error` receives the full diagnostic line (already prefixed).
auto parse_priority(std::string_view text, std::string& error) -> std::optional<int> {
  if (auto whole = parse_small_int(text, 191)) return whole;
  std::string_view facility_part;
  std::string_view severity_part = text;
  if (auto dot = text.rfind('.'); dot != std::string_view::npos) {
    facility_part = text.substr(0, dot);
    severity_part = text.substr(dot + 1);
  }
  int facility = 1;  // user
  if (!facility_part.empty()) {
    if (auto f = facility_value(facility_part)) {
      facility = *f;
    } else if (auto f = parse_small_int(facility_part, 23)) {
      facility = *f;
    } else {
      error = "logger: unknown facility name: " + std::string(facility_part);
      return std::nullopt;
    }
  }
  if (auto s = severity_value(severity_part)) {
    return facility * 8 + *s;
  }
  if (auto s = parse_small_int(severity_part, 7)) {
    return facility * 8 + *s;
  }
  error = "logger: unknown priority name: " + std::string(severity_part);
  return std::nullopt;
}

auto event_type_for_severity(int severity) -> WORD {
  if (severity <= 3) return EVENTLOG_ERROR_TYPE;
  if (severity == 4) return EVENTLOG_WARNING_TYPE;
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

auto write_event_log(const std::string& rendered, int severity) -> bool {
  HANDLE raw = RegisterEventSourceW(nullptr, L"WinuxCmd");
  if (raw == nullptr) return false;
  UniqueHandle source(raw);

  std::wstring text = utf8_to_wstring(rendered);
  const wchar_t* strings[] = {text.c_str()};
  BOOL ok = ReportEventW(source.get(), event_type_for_severity(severity), 0, 0,
                         nullptr, 1, 0, strings, nullptr);
  DeregisterEventSource(source.release());
  return ok != FALSE;
}

auto default_tag() -> std::string {
  wchar_t buffer[256];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (GetUserNameW(buffer, &size) != FALSE && size > 1) {
    return wstring_to_utf8(std::wstring_view(buffer, size - 1));
  }
  return "logger";
}

auto local_hostname() -> std::string {
  wchar_t buffer[256];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (GetComputerNameW(buffer, &size) != FALSE && size > 0) {
    return wstring_to_utf8(std::wstring_view(buffer, size));
  }
  return "-";
}

struct Entry {
  std::string text;
  int priority;
};

// [GNU] when the message comes from a file or stdin, EACH LINE is its own
// log entry (logger.c read loop); -e skips empty lines there, and
// --prio-prefix lets a leading "<pri>" on a line override its priority.
auto collect_stream_entries(std::istream& input, const Config& cfg,
                            std::vector<Entry>& entries) -> void {
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    int priority = cfg.priority;
    if (cfg.prio_prefix && line.size() > 2 && line.front() == '<') {
      if (auto close = line.find('>'); close != std::string::npos &&
                                       close <= 5) {
        if (auto pri = parse_small_int(
                std::string_view(line).substr(1, close - 1), 191)) {
          priority = *pri;
          line = line.substr(close + 1);
        }
      }
    }
    if (cfg.skip_empty && line.empty()) continue;
    entries.push_back(Entry{line, priority});
  }
}

auto rfc3164_timestamp() -> std::string {
  static constexpr std::array<const char*, 12> months = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%s %2d %02d:%02d:%02d",
                months[static_cast<size_t>(tm.tm_mon)], tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

auto rfc5424_timestamp() -> std::string {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  TIME_ZONE_INFORMATION tzi{};
  GetTimeZoneInformation(&tzi);
  long bias = -static_cast<long>(tzi.Bias);
  char sign = bias < 0 ? '-' : '+';
  long abs_bias = bias < 0 ? -bias : bias;
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02ld:%02ld",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, sign, abs_bias / 60, abs_bias % 60);
  return buf;
}

// Send one formatted wire message to the configured syslog server.
// Returns false (with `error` set) when resolution/connect/send fails;
// the oracle dies in that case (logger.c:312,329).
auto send_remote(const Config& cfg, const std::string& wire, std::string& error)
    -> bool {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = cfg.use_tcp ? SOCK_STREAM : SOCK_DGRAM;
  addrinfo* results = nullptr;
  std::string port =
      cfg.port == "syslog" ? std::string("514") : cfg.port;
  if (getaddrinfo(cfg.server.c_str(), port.c_str(), &hints, &results) != 0 ||
      results == nullptr) {
    error = "logger: failed to resolve name " + cfg.server + " port " + port;
    return false;
  }
  SOCKET sock = socket(results->ai_family, results->ai_socktype,
                       results->ai_protocol);
  if (sock == INVALID_SOCKET) {
    freeaddrinfo(results);
    error = "logger: failed to connect to " + cfg.server + " port " + port;
    return false;
  }
  bool ok = true;
  if (cfg.use_tcp) {
    if (connect(sock, results->ai_addr,
                static_cast<int>(results->ai_addrlen)) == SOCKET_ERROR) {
      ok = false;
    } else {
      // [GNU] TCP framing appends a newline (octet stuffing) unless
      // --octet-counting selects the RFC 6587 length prefix
      // (logger.c:448-479).
      std::string framed = cfg.octet_count
                               ? std::to_string(wire.size()) + " " + wire
                               : wire + "\n";
      ok = send(sock, framed.data(), static_cast<int>(framed.size()), 0) !=
           SOCKET_ERROR;
    }
  } else {
    ok = sendto(sock, wire.data(), static_cast<int>(wire.size()), 0,
                results->ai_addr,
                static_cast<int>(results->ai_addrlen)) != SOCKET_ERROR;
  }
  closesocket(sock);
  freeaddrinfo(results);
  if (!ok) {
    error = "logger: failed to connect to " + cfg.server + " port " + port;
  }
  return ok;
}

auto run(const CommandContext<LOGGER_OPTIONS.size()>& ctx) -> int {
  // [GNU] -u/--socket and --journald address Unix syslog facilities
  // with no Windows counterpart; refuse explicitly instead of silently
  // logging somewhere else.
  if (ctx.has("-u") || ctx.has("--socket")) {
    safeErrorPrintLn("logger: unix sockets are not supported on Windows");
    return 1;
  }
  if (ctx.has("--journald")) {
    safeErrorPrintLn("logger: --journald is not supported on Windows");
    return 1;
  }

  Config cfg;
  cfg.stderr_too =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--stderr", false);
  cfg.include_pid = ctx.get<bool>("-i", false) || ctx.get<bool>("--id", false);
  cfg.skip_empty =
      ctx.get<bool>("-e", false) || ctx.get<bool>("--skip-empty", false);
  cfg.no_act = ctx.get<bool>("--no-act", false);
  cfg.prio_prefix = ctx.get<bool>("--prio-prefix", false);
  cfg.rfc5424 = ctx.get<bool>("--rfc5424", false);
  cfg.rfc3164 = ctx.get<bool>("--rfc3164", false);
  cfg.octet_count = ctx.get<bool>("--octet-counting", false);
  cfg.msgid = ctx.get<std::string>("--msgid", "");
  cfg.use_tcp = ctx.get<bool>("-T", false) || ctx.get<bool>("--tcp", false);

  // [GNU] RFC 5424 structured data (logger.c:590-612): each --sd-id
  // starts an element, following --sd-param values attach to it; a
  // param before any id and a repeated id are hard errors.
  {
    std::vector<std::pair<std::string, std::vector<std::string>>> elements;
    for (const auto& occurrence :
         ctx.string_occurrences({"--sd-id", "--sd-param"})) {
      if (occurrence.short_name == "--sd-id" ||
          occurrence.long_name == "--sd-id") {
        for (const auto& [id, _] : elements) {
          if (id == occurrence.value) {
            safeErrorPrintLn("logger: structured data ID '" +
                             occurrence.value + "' is not unique");
            return 1;
          }
        }
        elements.push_back({occurrence.value, {}});
      } else {
        if (elements.empty()) {
          safeErrorPrintLn("logger: --sd-id was not specified for "
                           "--sd-param " +
                           occurrence.value);
          return 1;
        }
        elements.back().second.push_back(occurrence.value);
      }
    }
    for (const auto& [id, params] : elements) {
      cfg.structured_data += "[" + id;
      for (const auto& param : params) cfg.structured_data += " " + param;
      cfg.structured_data += "]";
    }
  }
  cfg.file = ctx.get<std::string>("-f", ctx.get<std::string>("--file", ""));
  cfg.server =
      ctx.get<std::string>("-n", ctx.get<std::string>("--server", ""));
  cfg.port = ctx.get<std::string>("-P", ctx.get<std::string>("--port", "514"));
  std::string tag_opt =
      ctx.get<std::string>("-t", ctx.get<std::string>("--tag", ""));
  // [GNU] the default tag is the login name (logger.c main: tag defaults
  // to the user's name), not the literal string "logger".
  cfg.tag = tag_opt.empty() ? default_tag() : tag_opt;

  std::string priority_text = ctx.get<std::string>(
      "-p", ctx.get<std::string>("--priority", "user.notice"));
  {
    std::string error;
    auto pri = parse_priority(priority_text, error);
    if (!pri) {
      safeErrorPrintLn(error);
      return 1;
    }
    cfg.priority = *pri;
  }
  {
    std::string size_text =
        ctx.get<std::string>("-S", ctx.get<std::string>("--size", ""));
    if (!size_text.empty()) {
      // [GNU] --size goes through strtosize_or_err (logger.c:1208):
      // a plain number with an optional K/M/G suffix (powers of 1024).
      auto sz = parse_small_int(size_text, 1 << 24);
      size_t multiplier = 1;
      if (!sz && size_text.size() > 1) {
        const char suffix = size_text.back();
        const size_t shift =
            suffix == 'K' ? 10 : suffix == 'M' ? 20 : suffix == 'G' ? 30 : 0;
        if (shift != 0) {
          if (auto base = parse_small_int(size_text.substr(0, size_text.size() - 1),
                                          1 << 24)) {
            sz = *base;
            multiplier = size_t{1} << shift;
          }
        }
      }
      if (sz) {
        cfg.max_size = static_cast<size_t>(*sz) * multiplier;
      } else {
        safeErrorPrintLn("logger: failed to parse size: '" + size_text + "'");
        return 1;
      }
    }
  }

  // Gather the entries: a positional message is ONE entry; a file or
  // stdin contributes one entry per line.
  std::vector<Entry> entries;
  std::string message = join_message(ctx.positionals);
  if (!message.empty()) {
    entries.push_back(Entry{message, cfg.priority});
  } else if (!cfg.file.empty()) {
    std::ifstream input(std::filesystem::u8path(
        native_path::normalize_api_operand(cfg.file)));
    if (!input) {
      std::error_code ec;
      bool is_dir = std::filesystem::is_directory(
          std::filesystem::u8path(native_path::normalize_api_operand(cfg.file)),
          ec);
      safeErrorPrintLn("logger: " + cfg.file +
                       (is_dir ? ": Is a directory"
                               : ": No such file or directory"));
      return 1;
    }
    collect_stream_entries(input, cfg, entries);
  } else {
    collect_stream_entries(std::cin, cfg, entries);
  }

  const std::string pid = std::to_string(GetCurrentProcessId());
  const std::string display_tag =
      cfg.include_pid ? cfg.tag + "[" + pid + "]" : cfg.tag;
  const bool remote = !cfg.server.empty();

  WSADATA wsa{};
  bool wsa_started = false;
  if (remote && !cfg.no_act) {
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
      wsa_started = true;
    } else {
      safeErrorPrintLn("logger: failed to connect to " + cfg.server +
                       " port " + cfg.port);
      return 1;
    }
  }

  int status = 0;
  for (const auto& entry : entries) {
    std::string text = entry.text;
    if (cfg.max_size > 0 && text.size() > cfg.max_size) {
      text.resize(cfg.max_size);
    }
    const std::string rendered = display_tag + ": " + text;
    if (!cfg.no_act) {
      if (remote) {
        std::string wire;
        // [GNU] RFC 5424 is the default protocol for remote servers;
        // --rfc3164 selects the obsolete BSD format (logger.c usage,
        // "the syslog protocol (the default for remote)").
        if (cfg.rfc5424 || !cfg.rfc3164) {
          wire = "<" + std::to_string(entry.priority) + ">1 " +
                 rfc5424_timestamp() + " " + local_hostname() + " " + cfg.tag +
                 " " + (cfg.include_pid ? pid : std::string("-")) + " " +
                 (cfg.msgid.empty() ? std::string("-") : cfg.msgid) + " " +
                 (cfg.structured_data.empty() ? std::string("-")
                                              : cfg.structured_data) +
                 " " + text;
        } else {
          wire = "<" + std::to_string(entry.priority) + ">" +
                 rfc3164_timestamp() + " " + local_hostname() + " " +
                 display_tag + ": " + text;
        }
        std::string error;
        if (!send_remote(cfg, wire, error)) {
          safeErrorPrintLn(error);
          status = 1;
          break;
        }
      } else {
        OutputDebugStringW(utf8_to_wstring(rendered).c_str());
        // [GNU] a failed write is a failed command (logger exits
        // nonzero when the log write fails).
        if (!write_event_log(rendered, entry.priority & 7)) status = 1;
      }
    }
    if (cfg.stderr_too) safeErrorPrintLn(rendered);
  }

  if (wsa_started) WSACleanup();
  return status;
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
