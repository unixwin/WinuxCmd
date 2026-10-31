// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Windows-native logger command.
/// @Version: 0.1.0
/// @License: MIT

#include <afunix.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "ws2_32.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LOGGER_OPTIONS = std::array{
    // [GNU] -i, --id[=PID]: log the PID (logger's own, or PID when given)
    OPTION("-i", "--id", "log the PID of the logger process",
           OPTIONAL_STRING_TYPE),
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
    // [GNU] -u, --socket: write to this UNIX socket (logger.c:1140)
    OPTION("-u", "--socket", "write to this UNIX socket instead of the log",
           STRING_TYPE),
    // [GNU] --socket-errors MODE: on, off, or auto (logger.c:1141,891)
    OPTION("", "--socket-errors", "when to report socket errors: on, off, auto",
           STRING_TYPE),
    // [GNU] --msgid: set the RFC 5424 MSGID field
    OPTION("", "--msgid", "set the RFC 5424 MSGID field", STRING_TYPE),
    // [GNU] --octet-count: use RFC 6587 octet counting for TCP framing
    OPTION("", "--octet-count", "use octet counting for TCP framing"),
    // [GNU] --sd-id: RFC 5424 structured data element id
    OPTION("", "--sd-id", "structured data element id (name[@enterprise])",
           STRING_TYPE),
    // [GNU] --sd-param: parameter for the preceding --sd-id element
    OPTION("", "--sd-param", "structured data parameter (name=\"value\")",
           STRING_TYPE),
    // [GNU] --journald[=FILE]: write a systemd journal entry
    OPTION("", "--journald", "write a journal entry read from FILE or stdin",
           OPTIONAL_STRING_TYPE),
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
namespace cp = core::pipeline;

struct SdElement {
  std::string id;
  std::vector<std::string> params;
};

struct Config {
  int pri = 1 * 8 + 5;  // LOG_USER | LOG_NOTICE (logger.c:1111)
  std::string tag;
  bool stderr_too = false;
  bool skip_empty = false;
  bool prio_prefix = false;
  bool no_act = false;
  bool use_tcp = false;
  std::string server;
  std::string port = "514";
  std::string socket_path;
  std::string socket_errors = "auto";
  bool rfc3164 = false;
  bool rfc5424 = false;
  bool octet_count = false;
  std::string msgid;
  std::vector<SdElement> sd;
  size_t max_size = 0;  // 0 = no explicit -S cap
  std::string file;
  bool log_pid = false;
  DWORD pid_value = 0;
  bool journald = false;
  std::string journald_file;
  std::string message;
  bool has_message_args = false;
};

// Facility/severity tables (syslog names, as in logger.c's pencode).
auto facility_number(std::string_view name) -> int {
  static constexpr std::pair<std::string_view, int> kFacilities[] = {
      {"kern", 0},    {"user", 1},     {"mail", 2},    {"daemon", 3},
      {"auth", 4},    {"security", 4}, {"syslog", 5},  {"lpr", 6},
      {"news", 7},    {"uucp", 8},     {"cron", 9},    {"authpriv", 10},
      {"ftp", 11},    {"ntp", 12},     {"local0", 16}, {"local1", 17},
      {"local2", 18}, {"local3", 19},  {"local4", 20}, {"local5", 21},
      {"local6", 22}, {"local7", 23}};
  for (const auto& [text, value] : kFacilities) {
    if (name == text) return value;
  }
  return -1;
}

auto severity_number(std::string_view name) -> int {
  static constexpr std::pair<std::string_view, int> kSeverities[] = {
      {"emerg", 0},  {"panic", 0}, {"alert", 1},   {"crit", 2},
      {"err", 3},    {"error", 3}, {"warning", 4}, {"warn", 4},
      {"notice", 5}, {"info", 6},  {"debug", 7}};
  for (const auto& [text, value] : kSeverities) {
    if (name == text) return value;
  }
  return -1;
}

// [GNU] pencode(): "facility.severity", a bare severity (facility user),
// a bare facility (severity notice), or a plain number.
auto parse_priority(std::string_view text, int& out) -> bool {
  if (text.empty()) return false;
  if (std::ranges::all_of(
          text, [](unsigned char c) { return std::isdigit(c) != 0; })) {
    int value = 0;
    auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size() || value < 0 ||
        value > 191) {
      return false;
    }
    out = value;
    return true;
  }
  const std::string lower = ascii_lower_copy(text);
  if (auto dot = lower.find('.'); dot != std::string::npos) {
    const int fac = facility_number(lower.substr(0, dot));
    const int sev = severity_number(lower.substr(dot + 1));
    if (fac < 0 || sev < 0) return false;
    out = fac * 8 + sev;
    return true;
  }
  if (const int sev = severity_number(lower); sev >= 0) {
    out = 1 * 8 + sev;
    return true;
  }
  if (const int fac = facility_number(lower); fac >= 0) {
    out = fac * 8 + 5;
    return true;
  }
  return false;
}

// [GNU] Default tag is the login name (logger.c:945), "<someone>" when
// it cannot be determined (logger.c:947).
auto default_tag() -> std::string {
  char name[256];
  DWORD size = static_cast<DWORD>(std::size(name));
  if (GetUserNameA(name, &size) && size > 1) {
    return std::string(name, size - 1);
  }
  return "<someone>";
}

auto local_hostname() -> std::string {
  char name[256];
  DWORD size = static_cast<DWORD>(std::size(name));
  if (GetComputerNameExA(ComputerNameDnsHostname, name, &size) && size > 0) {
    return std::string(name, size);
  }
  size = static_cast<DWORD>(std::size(name));
  if (GetComputerNameA(name, &size) && size > 0) {
    return std::string(name, size);
  }
  return "-";
}

// [GNU] RFC 3164 timestamp: "Mmm dd hh:mm:ss" local time, day padded
// with a space.
auto rfc3164_time() -> std::string {
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return std::format("{} {:2} {:02}:{:02}:{:02}", kMonths[(st.wMonth - 1) % 12],
                     st.wDay, st.wHour, st.wMinute, st.wSecond);
}

// [GNU] RFC 5424 timestamp (logger.c:796-810): local ISO 8601 with
// microseconds and an RFC 3339 numeric offset.
auto rfc5424_time() -> std::string {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  TIME_ZONE_INFORMATION tzi{};
  const DWORD zone = GetTimeZoneInformation(&tzi);
  LONG bias = tzi.Bias;
  if (zone == TIME_ZONE_ID_DAYLIGHT) {
    bias += tzi.DaylightBias;
  } else if (zone == TIME_ZONE_ID_STANDARD) {
    bias += tzi.StandardBias;
  }
  const int offset_minutes = -bias;
  const char sign = offset_minutes < 0 ? '-' : '+';
  const int abs_offset = offset_minutes < 0 ? -offset_minutes : offset_minutes;
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:06}{}{:02}:{:02}",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                     st.wSecond,
                     static_cast<unsigned long>(st.wMilliseconds) * 1000, sign,
                     abs_offset / 60, abs_offset % 60);
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

auto build_config(const CommandContext<LOGGER_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  std::string priority_text =
      ctx.get<std::string>("-p", ctx.get<std::string>("--priority", ""));
  if (!priority_text.empty() && !parse_priority(priority_text, cfg.pri)) {
    return std::unexpected("unknown priority name: " + priority_text);
  }

  cfg.tag = ctx.get<std::string>("-t", ctx.get<std::string>("--tag", ""));
  if (cfg.tag.empty()) cfg.tag = default_tag();
  cfg.stderr_too =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--stderr", false);
  cfg.skip_empty =
      ctx.get<bool>("-e", false) || ctx.get<bool>("--skip-empty", false);
  cfg.prio_prefix = ctx.get<bool>("--prio-prefix", false);
  cfg.no_act = ctx.get<bool>("--no-act", false);
  cfg.server = ctx.get<std::string>("-n", "");
  if (cfg.server.empty()) cfg.server = ctx.get<std::string>("--server", "");
  cfg.port = ctx.get<std::string>("-P", "");
  if (cfg.port.empty()) cfg.port = ctx.get<std::string>("--port", "514");
  cfg.socket_path = ctx.get<std::string>("-u", "");
  if (cfg.socket_path.empty()) {
    cfg.socket_path = ctx.get<std::string>("--socket", "");
  }

  std::string errors_mode = ctx.get<std::string>("--socket-errors", "");
  if (!errors_mode.empty()) {
    std::string lower = ascii_lower_copy(errors_mode);
    if (lower == "on" || lower == "off" || lower == "auto") {
      cfg.socket_errors = lower;
    } else {
      // [GNU] logger.c:897: warn and fall back to automatic errors.
      safeErrorPrintLn("logger: invalid argument: " + errors_mode +
                       ": using automatic errors");
    }
  }

  cfg.rfc3164 = ctx.get<bool>("--rfc3164", false);
  cfg.rfc5424 = ctx.get<bool>("--rfc5424", false);
  cfg.octet_count = ctx.get<bool>("--octet-count", false);
  cfg.msgid = ctx.get<std::string>("--msgid", "");

  // Structured data: --sd-param entries attach to the most recent
  // --sd-id element, in command-line order (logger.c list semantics).
  for (const auto& occurrence :
       ctx.string_occurrences({"--sd-id", "--sd-param"})) {
    if (occurrence.long_name == "--sd-id") {
      cfg.sd.push_back(SdElement{occurrence.value, {}});
    } else if (!cfg.sd.empty()) {
      cfg.sd.back().params.push_back(occurrence.value);
    }
  }

  std::string size_text =
      ctx.get<std::string>("-S", ctx.get<std::string>("--size", ""));
  if (!size_text.empty()) {
    unsigned long value = 0;
    auto [ptr, ec] = std::from_chars(
        size_text.data(), size_text.data() + size_text.size(), value);
    if (ec != std::errc() || ptr != size_text.data() + size_text.size()) {
      return std::unexpected("invalid size argument: " + size_text);
    }
    cfg.max_size = value;
  }

  cfg.file = ctx.get<std::string>("-f", "");
  if (cfg.file.empty()) cfg.file = ctx.get<std::string>("--file", "");

  // [GNU] -i/--id: bare flag logs our own PID; --id=PID logs that PID
  // (logger.c:1134, optional argument).
  if (ctx.has("-i") || ctx.has("--id")) {
    cfg.log_pid = true;
    cfg.pid_value = GetCurrentProcessId();
    std::string id_text = ctx.get<std::string>("--id", "");
    if (!id_text.empty()) {
      unsigned long value = 0;
      auto [ptr, ec] = std::from_chars(id_text.data(),
                                       id_text.data() + id_text.size(), value);
      if (ec != std::errc() || ptr != id_text.data() + id_text.size()) {
        return std::unexpected("invalid id argument: " + id_text);
      }
      cfg.pid_value = value;
    }
  }

  // -T/--tcp vs -d/--udp: the last one given wins (logger.c processes
  // the flags sequentially).
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= LOGGER_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-T" || meta.long_name == "--tcp") {
      cfg.use_tcp = true;
    }
    if (meta.short_name == "-d" || meta.long_name == "--udp") {
      cfg.use_tcp = false;
    }
  }

  cfg.journald = ctx.has("--journald");
  if (cfg.journald) {
    cfg.journald_file = ctx.get<std::string>("--journald", "");
  }

  cfg.message = join_message(ctx.positionals);
  cfg.has_message_args = !ctx.positionals.empty();

  return cfg;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// The "tag[pid]:" prefix used for the local log and -s output; the PID
// bracket appears only with -i/--id (logger.c:903-911).
auto local_prefix(const Config& cfg, const std::string& tag) -> std::string {
  std::string out = tag;
  if (cfg.log_pid) {
    out += "[" + std::to_string(cfg.pid_value) + "]";
  }
  out += ": ";
  return out;
}

// [GNU] RFC 3164 wire form. With a hostname (remote server) the layout
// is "<PRI>Mmm dd hh:mm:ss HOST TAG[pid]: MSG"; for a local socket the
// hostname is omitted (logger.c:903-911).
auto render_rfc3164(const Config& cfg, int pri, const std::string& message,
                    bool with_hostname, const std::string& tag) -> std::string {
  std::string out = "<" + std::to_string(pri) + ">" + rfc3164_time() + " ";
  if (with_hostname) {
    out += local_hostname() + " ";
  }
  std::string effective = tag;
  if (cfg.log_pid) {
    effective += "[" + std::to_string(cfg.pid_value) + "]";
  }
  out += effective + ": " + message;
  return out;
}

auto sd_escape(std::string_view text) -> std::string {
  std::string out;
  for (char c : text) {
    if (c == '\\' || c == '"' || c == ']') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

// [GNU] RFC 5424 wire form (logger.c:783ff): the reserved timeQuality
// element precedes user elements unless the user redefines it.
auto render_rfc5424(const Config& cfg, int pri, const std::string& message,
                    const std::string& tag) -> std::string {
  std::string out = "<" + std::to_string(pri) + ">1 " + rfc5424_time() + " " +
                    local_hostname() + " ";
  out += (tag.empty() ? std::string("-") : tag);
  out += " ";
  out += cfg.log_pid ? std::to_string(cfg.pid_value) : "-";
  out += " ";
  out += cfg.msgid.empty() ? std::string("-") : cfg.msgid;
  out += " ";

  std::string sd;
  bool user_has_time_quality = false;
  for (const auto& element : cfg.sd) {
    if (element.id == "timeQuality") user_has_time_quality = true;
  }
  if (!user_has_time_quality) {
    sd += "[timeQuality tzKnown=\"1\"]";
  }
  for (const auto& element : cfg.sd) {
    sd += "[" + sd_escape(element.id);
    for (const auto& param : element.params) {
      sd += " " + sd_escape(param);
    }
    sd += "]";
  }
  out += sd.empty() ? std::string("-") : sd;
  out += " " + message;
  return out;
}

// ---------------------------------------------------------------------------
// Destinations
// ---------------------------------------------------------------------------

struct WsaSession {
  bool started = false;
  WsaSession() {
    WSADATA data{};
    started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WsaSession() {
    if (started) WSACleanup();
  }
  WsaSession(const WsaSession&) = delete;
  WsaSession& operator=(const WsaSession&) = delete;
};

struct Destination {
  bool network = false;  // server or unix socket
  bool unix_socket = false;
  SOCKET sock = INVALID_SOCKET;
  bool tcp = false;

  ~Destination() {
    if (sock != INVALID_SOCKET) closesocket(sock);
  }
  Destination() = default;
  Destination(const Destination&) = delete;
  Destination& operator=(const Destination&) = delete;
};

auto open_destination(const Config& cfg, std::string& error)
    -> std::unique_ptr<Destination> {
  auto dest = std::make_unique<Destination>();
  if (cfg.socket_path.empty() && cfg.server.empty()) {
    return dest;  // local event-log destination
  }
  dest->network = true;

  if (!cfg.socket_path.empty()) {
    dest->unix_socket = true;
    SOCKET raw = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (raw == INVALID_SOCKET) {
      error = "cannot create UNIX socket: error " +
              std::to_string(WSAGetLastError());
      return nullptr;
    }
    dest->sock = raw;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string path =
        native_path::normalize_api_operand(cfg.socket_path);
    if (path.size() >= std::size(addr.sun_path)) {
      error = "UNIX socket path is too long: " + cfg.socket_path;
      return nullptr;
    }
    std::ranges::copy(path, addr.sun_path);
    if (connect(raw, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
      error = "cannot connect to UNIX socket '" + cfg.socket_path +
              "': error " + std::to_string(WSAGetLastError());
      return nullptr;
    }
    return dest;
  }

  dest->tcp = cfg.use_tcp;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = cfg.use_tcp ? SOCK_STREAM : SOCK_DGRAM;
  addrinfo* result = nullptr;
  if (getaddrinfo(cfg.server.c_str(), cfg.port.c_str(), &hints, &result) != 0 ||
      result == nullptr) {
    error = "cannot resolve server '" + cfg.server + "'";
    return nullptr;
  }
  SOCKET raw =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (raw == INVALID_SOCKET) {
    freeaddrinfo(result);
    error = "cannot create socket: error " + std::to_string(WSAGetLastError());
    return nullptr;
  }
  dest->sock = raw;
  const int rc =
      connect(raw, result->ai_addr, static_cast<int>(result->ai_addrlen));
  freeaddrinfo(result);
  if (rc == SOCKET_ERROR) {
    error = "cannot connect to server '" + cfg.server + "': error " +
            std::to_string(WSAGetLastError());
    return nullptr;
  }
  return dest;
}

auto send_payload(Destination& dest, const std::string& payload,
                  std::string& error) -> bool {
  const char* cursor = payload.data();
  size_t remaining = payload.size();
  while (remaining > 0) {
    const int chunk = send(dest.sock, cursor, static_cast<int>(remaining), 0);
    if (chunk == SOCKET_ERROR) {
      error = "write error: " + std::to_string(WSAGetLastError());
      return false;
    }
    cursor += chunk;
    remaining -= static_cast<size_t>(chunk);
  }
  return true;
}

auto write_event_log(const Config& cfg, int pri,
                     const std::string& rendered_text) -> bool {
  HANDLE raw = RegisterEventSourceW(nullptr, L"WinuxCmd");
  if (raw == nullptr) return false;
  UniqueHandle source(raw);

  std::wstring text = utf8_to_wstring(rendered_text);
  const wchar_t* strings[] = {text.c_str()};
  BOOL ok = ReportEventW(source.get(), event_type_for_severity(pri & 7), 0, 0,
                         nullptr, 1, 0, strings, nullptr);
  DeregisterEventSource(source.release());
  return ok != FALSE;
}

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

struct Delivery {
  const Config& cfg;
  Destination* dest = nullptr;
  bool failed = false;
  std::string first_error;

  auto report_errors() const -> bool {
    if (cfg.socket_errors == "on") return true;
    if (cfg.socket_errors == "off") return false;
    // auto (logger.c:1305): report when --no-act or -s is in play.
    return cfg.no_act || cfg.stderr_too;
  }

  auto deliver(int pri, const std::string& message, const std::string& tag)
      -> void {
    // [GNU] -s output happens even under --no-act; the writes do not
    // (logger.c write_output order).
    if (cfg.stderr_too) {
      safeErrorPrintLn(local_prefix(cfg, tag) + message);
    }
    if (cfg.no_act) return;

    if (dest != nullptr && dest->network) {
      const bool use_5424 = cfg.rfc5424 || (!cfg.rfc3164 && !dest->unix_socket);
      std::string payload =
          use_5424 ? render_rfc5424(cfg, pri, message, tag)
                   : render_rfc3164(cfg, pri, message, !dest->unix_socket, tag);
      std::string framed = payload;
      if (dest->tcp) {
        if (cfg.octet_count) {
          framed = std::to_string(payload.size()) + " " + payload;
        } else {
          framed = payload + "\n";
        }
      }
      std::string error;
      if (!send_payload(*dest, framed, error)) {
        failed = true;
        if (first_error.empty()) first_error = error;
      }
      return;
    }

    OutputDebugStringW(
        utf8_to_wstring(local_prefix(cfg, tag) + message).c_str());
    if (!write_event_log(cfg, pri, local_prefix(cfg, tag) + message)) {
      failed = true;
      if (first_error.empty()) {
        first_error = "cannot write to the Windows event log";
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

// [GNU] logger_stdin (logger.c:990ff): every input line is its own
// message; with --prio-prefix a leading "<NNN>" (0..191) overrides the
// priority (facility 0 falls back to the default facility); -e drops
// empty lines; lines are capped at the message size.
auto deliver_lines(const Config& cfg, Delivery& delivery, std::istream& input,
                   int default_pri) -> void {
  const size_t cap = cfg.max_size > 0 ? cfg.max_size : 1024;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    int pri = default_pri;
    if (cfg.prio_prefix && line.size() >= 3 && line.front() == '<') {
      size_t close = line.find('>');
      if (close != std::string::npos && close > 1 && close <= 4) {
        int value = 0;
        const std::string digits = line.substr(1, close - 1);
        auto [ptr, ec] = std::from_chars(digits.data(),
                                         digits.data() + digits.size(), value);
        if (ec == std::errc() && ptr == digits.data() + digits.size() &&
            value >= 0 && value <= 191) {
          pri = value;
          if ((pri & 0xF8) == 0) pri |= default_pri & 0xF8;
          line = line.substr(close + 1);
        }
      }
    }
    if (line.size() > cap) line.resize(cap);
    if (line.empty() && cfg.skip_empty) continue;
    delivery.deliver(pri, line, cfg.tag);
  }
}

// [GNU] --journald entry (logger.c:338ff): FIELD=value lines up to a
// blank line form one entry; repeated MESSAGE= fields join with '\n'.
// The journal sink does not exist on Windows, so the entry is mapped
// onto the port's sinks: MESSAGE is the text, PRIORITY the severity,
// SYSLOG_IDENTIFIER the tag.
auto deliver_journald(const Config& cfg, Delivery& delivery,
                      std::istream& input) -> void {
  std::string message;
  std::string tag = cfg.tag;
  int pri = cfg.pri;
  bool have_fields = false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;  // blank line ends the entry
    have_fields = true;
    if (cfg.stderr_too) {
      safeErrorPrintLn(line);
    }
    if (auto eq = line.find('='); eq != std::string::npos) {
      const std::string field = line.substr(0, eq);
      const std::string value = line.substr(eq + 1);
      if (field == "MESSAGE") {
        if (!message.empty()) message.push_back('\n');
        message += value;
      } else if (field == "PRIORITY") {
        int severity = 0;
        auto [ptr, ec] = std::from_chars(value.data(),
                                         value.data() + value.size(), severity);
        if (ec == std::errc() && ptr == value.data() + value.size() &&
            severity >= 0 && severity <= 7) {
          pri = (cfg.pri & 0xF8) | severity;
        }
      } else if (field == "SYSLOG_IDENTIFIER") {
        tag = value;
      }
    }
  }
  if (!have_fields) return;
  delivery.deliver(pri, message, tag);
}

auto run(const CommandContext<LOGGER_OPTIONS.size()>& ctx) -> int {
  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"logger");
    return 1;
  }
  Config cfg = *cfg_result;

  WsaSession wsa;
  std::string open_error;
  auto dest = open_destination(cfg, open_error);
  if (!dest) {
    if (cfg.socket_errors != "off") {
      safeErrorPrintLn("logger: " + open_error);
    }
    return 1;
  }
  Delivery delivery{cfg, dest.get()};

  if (cfg.journald) {
    if (!cfg.journald_file.empty()) {
      std::ifstream journal_file(
          native_path::normalize_api_operand(cfg.journald_file));
      if (!journal_file) {
        safeErrorPrintLn("logger: cannot open journal file '" +
                         cfg.journald_file + "'");
        return 1;
      }
      deliver_journald(cfg, delivery, journal_file);
    } else {
      deliver_journald(cfg, delivery, std::cin);
    }
  } else if (!cfg.file.empty()) {
    // [GNU] -f: log the file's lines (it wins over message arguments).
    std::ifstream file(native_path::normalize_api_operand(cfg.file));
    if (!file) {
      safeErrorPrintLn("logger: cannot open file '" + cfg.file + "'");
      return 1;
    }
    deliver_lines(cfg, delivery, file, cfg.pri);
  } else if (cfg.has_message_args) {
    std::string message = cfg.message;
    if (cfg.max_size > 0 && message.size() > cfg.max_size) {
      message.resize(cfg.max_size);
    }
    delivery.deliver(cfg.pri, message, cfg.tag);
  } else {
    deliver_lines(cfg, delivery, std::cin, cfg.pri);
  }

  if (delivery.failed) {
    if (delivery.report_errors() && !delivery.first_error.empty()) {
      safeErrorPrintLn("logger: " + delivery.first_error);
    }
    return 1;
  }
  return 0;
}

}  // namespace logger_pipeline

REGISTER_COMMAND(logger, "logger", "logger [OPTION]... [MESSAGE]",
                 "Write messages to the system log.\n"
                 "\n"
                 "Messages are written to the Windows Event Log, or to a "
                 "remote\n"
                 "syslog server with -n/--server, or to a UNIX socket "
                 "with\n"
                 "-u/--socket.  Without a MESSAGE argument, each line of "
                 "standard\n"
                 "input (or of the -f file) is logged as its own message.  "
                 "The\n"
                 "default tag is the login name.  Wire formats are RFC "
                 "3164 and\n"
                 "RFC 5424 (--rfc5424 is the default for a remote server), "
                 "with\n"
                 "--msgid and --sd-id/--sd-param structured data, and RFC "
                 "6587\n"
                 "octet counting for TCP with --octet-count.  --journald "
                 "reads a\n"
                 "systemd journal entry (FIELD=value lines) and logs its "
                 "MESSAGE.",
                 "  logger -t build finished\n"
                 "  echo finished | logger -s\n"
                 "  logger -n loghost -p user.err disk full",
                 "eventcreate(1), regtool(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", LOGGER_OPTIONS) {
  return logger_pipeline::run(ctx);
}
