// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Shared process-selection engine for the procps family
///     (pgrep/pkill) plus the Linux signal vocabulary shared with the
///     psmisc killall command. The semantics mirror procps 4.0.4:
///     option criteria and the match predicate follow
///     procps-v4.0.4/src/pgrep.c select_procs(), and signal name
///     resolution follows procps local/signals.c
///     signal_name_to_number(). Only behavior facts are taken from the
///     oracle sources; this is an independent implementation.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#pragma once

#include "pch/pch.h"

namespace procps_select {

// ===========================================================================
// Linux signal vocabulary (procps local/signals.c)
// ===========================================================================

struct SignalEntry {
  int number;
  const char* name;
};

// Standard Linux signal table in numeric order (signal 16, STKFLT, is
// x86-specific upstream; it is part of the procps table).
inline constexpr std::array<SignalEntry, 31> kLinuxSignals{{
    {1, "HUP"},     {2, "INT"},   {3, "QUIT"},   {4, "ILL"},   {5, "TRAP"},
    {6, "ABRT"},    {7, "BUS"},   {8, "FPE"},    {9, "KILL"},  {10, "USR1"},
    {11, "SEGV"},   {12, "USR2"}, {13, "PIPE"},  {14, "ALRM"}, {15, "TERM"},
    {16, "STKFLT"}, {17, "CHLD"}, {18, "CONT"},  {19, "STOP"}, {20, "TSTP"},
    {21, "TTIN"},   {22, "TTOU"}, {23, "URG"},   {24, "XCPU"}, {25, "XFSZ"},
    {26, "VTALRM"}, {27, "PROF"}, {28, "WINCH"}, {29, "IO"},   {30, "PWR"},
    {31, "SYS"},
}};

inline auto ascii_iequals(std::string_view a, std::string_view b) -> bool {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
    if (ca != cb) return false;
  }
  return true;
}

inline auto all_digits(std::string_view text) -> bool {
  return !text.empty() &&
         std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

// Resolve a signal name or number the way procps
// local/signals.c signal_name_to_number() does (reached from pgrep.c
// through its static wrapper): a leading "SIG" is stripped
// case-insensitively, the aliases CLD/IO/IOT map to CHLD/POLL/ABRT,
// EXIT and NULL name signal 0, RTMIN/RTMAX accept +/- offsets, and a
// plain number in [0, 64] stands for itself. Returns -1 when the text
// does not name a signal.
inline auto signal_name_to_number(std::string_view name) -> int {
  if (name.empty()) return -1;
  if (all_digits(name)) {
    unsigned long long value = 0;
    for (char c : name) {
      value = value * 10 + static_cast<unsigned>(c - '0');
      if (value > 64) return -1;
    }
    return static_cast<int>(value);
  }
  std::string_view key = name;
  if (key.size() > 3 && ascii_iequals(key.substr(0, 3), "SIG")) {
    key.remove_prefix(3);
  }
  if (ascii_iequals(key, "EXIT") || ascii_iequals(key, "NULL")) return 0;
  if (ascii_iequals(key, "CLD")) return 17;  // SIGCHLD alias
  if (ascii_iequals(key, "IOT")) return 6;   // SIGABRT alias
  if (ascii_iequals(key, "POLL") || ascii_iequals(key, "LOST")) return 29;
  if (ascii_iequals(key, "UNUSED")) return 31;  // SIGSYS alias
  constexpr int kRtMin = 34;
  constexpr int kRtMax = 64;
  if (ascii_iequals(key, "RTMIN")) return kRtMin;
  if (ascii_iequals(key, "RTMAX")) return kRtMax;
  if (key.size() > 6 && ascii_iequals(key.substr(0, 5), "RTMIN") &&
      (key[5] == '+' || key[5] == '-')) {
    const bool plus = key[5] == '+';
    const auto digits = key.substr(6);
    if (!all_digits(digits)) return -1;
    int offset = 0;
    for (char c : digits) offset = offset * 10 + (c - '0');
    const int value = plus ? kRtMin + offset : kRtMin - offset;
    return (value >= kRtMin && value <= kRtMax) ? value : -1;
  }
  if (key.size() > 6 && ascii_iequals(key.substr(0, 5), "RTMAX") &&
      (key[5] == '+' || key[5] == '-')) {
    const bool plus = key[5] == '+';
    const auto digits = key.substr(6);
    if (!all_digits(digits)) return -1;
    int offset = 0;
    for (char c : digits) offset = offset * 10 + (c - '0');
    const int value = plus ? kRtMax + offset : kRtMax - offset;
    return (value >= kRtMin && value <= kRtMax) ? value : -1;
  }
  for (const auto& entry : kLinuxSignals) {
    if (ascii_iequals(key, entry.name)) return entry.number;
  }
  return -1;
}

// Display name for a signal number (killall -l): standard names from
// the table, RT signals in RTMIN+n / RTMAX-n form (procps style).
inline auto signal_display_name(int number) -> std::string {
  for (const auto& entry : kLinuxSignals) {
    if (entry.number == number) return entry.name;
  }
  if (number == 34) return "RTMIN";
  if (number == 64) return "RTMAX";
  if (number > 34 && number < 64) {
    if (number - 34 <= 64 - number) {
      return "RTMIN+" + std::to_string(number - 34);
    }
    return "RTMAX-" + std::to_string(64 - number);
  }
  return std::to_string(number);
}

// OPTION entries for the -SIGNAL spellings pkill/killall accept outside
// getopt upstream (pgrep.c signal_option() pre-scan). The descriptions
// are empty on purpose: empty-description metas are parser sentinels
// and never appear in --help (see format_help_text in cmd_meta.cppm).
#define PROCPS_SIGNAL_SPELLING_OPTIONS                                       \
  OPTION("-HUP", "", ""), OPTION("-SIGHUP", "", ""), OPTION("-INT", "", ""), \
      OPTION("-SIGINT", "", ""), OPTION("-QUIT", "", ""),                    \
      OPTION("-SIGQUIT", "", ""), OPTION("-ILL", "", ""),                    \
      OPTION("-SIGILL", "", ""), OPTION("-TRAP", "", ""),                    \
      OPTION("-SIGTRAP", "", ""), OPTION("-ABRT", "", ""),                   \
      OPTION("-SIGABRT", "", ""), OPTION("-BUS", "", ""),                    \
      OPTION("-SIGBUS", "", ""), OPTION("-FPE", "", ""),                     \
      OPTION("-SIGFPE", "", ""), OPTION("-KILL", "", ""),                    \
      OPTION("-SIGKILL", "", ""), OPTION("-USR1", "", ""),                   \
      OPTION("-SIGUSR1", "", ""), OPTION("-SEGV", "", ""),                   \
      OPTION("-SIGSEGV", "", ""), OPTION("-USR2", "", ""),                   \
      OPTION("-SIGUSR2", "", ""), OPTION("-PIPE", "", ""),                   \
      OPTION("-SIGPIPE", "", ""), OPTION("-ALRM", "", ""),                   \
      OPTION("-SIGALRM", "", ""), OPTION("-TERM", "", ""),                   \
      OPTION("-SIGTERM", "", ""), OPTION("-STKFLT", "", ""),                 \
      OPTION("-SIGSTKFLT", "", ""), OPTION("-CHLD", "", ""),                 \
      OPTION("-SIGCHLD", "", ""), OPTION("-CLD", "", ""),                    \
      OPTION("-SIGCLD", "", ""), OPTION("-CONT", "", ""),                    \
      OPTION("-SIGCONT", "", ""), OPTION("-STOP", "", ""),                   \
      OPTION("-SIGSTOP", "", ""), OPTION("-TSTP", "", ""),                   \
      OPTION("-SIGTSTP", "", ""), OPTION("-TTIN", "", ""),                   \
      OPTION("-SIGTTIN", "", ""), OPTION("-TTOU", "", ""),                   \
      OPTION("-SIGTTOU", "", ""), OPTION("-URG", "", ""),                    \
      OPTION("-SIGURG", "", ""), OPTION("-XCPU", "", ""),                    \
      OPTION("-SIGXCPU", "", ""), OPTION("-XFSZ", "", ""),                   \
      OPTION("-SIGXFSZ", "", ""), OPTION("-VTALRM", "", ""),                 \
      OPTION("-SIGVTALRM", "", ""), OPTION("-PROF", "", ""),                 \
      OPTION("-SIGPROF", "", ""), OPTION("-WINCH", "", ""),                  \
      OPTION("-SIGWINCH", "", ""), OPTION("-IO", "", ""),                    \
      OPTION("-SIGIO", "", ""), OPTION("-POLL", "", ""),                     \
      OPTION("-SIGPOLL", "", ""), OPTION("-LOST", "", ""),                   \
      OPTION("-SIGLOST", "", ""), OPTION("-PWR", "", ""),                    \
      OPTION("-SIGPWR", "", ""), OPTION("-SYS", "", ""),                     \
      OPTION("-SIGSYS", "", ""), OPTION("-UNUSED", "", ""),                  \
      OPTION("-SIGUNUSED", "", ""), OPTION("-IOT", "", ""),                  \
      OPTION("-SIGIOT", "", ""), OPTION("-EXIT", "", ""),                    \
      OPTION("-SIGEXIT", "", ""), OPTION("-NULL", "", ""),                   \
      OPTION("-SIGNULL", "", ""), OPTION("-RTMIN", "", ""),                  \
      OPTION("-SIGRTMIN", "", ""), OPTION("-RTMAX", "", ""),                 \
      OPTION("-SIGRTMAX", "", "")

// Short names of every spelling meta above, for occurrence counting.
inline constexpr std::array<std::string_view, 80> kSignalSpellingNames{{
    "-HUP",    "-SIGHUP",    "-INT",    "-SIGINT",    "-QUIT",   "-SIGQUIT",
    "-ILL",    "-SIGILL",    "-TRAP",   "-SIGTRAP",   "-ABRT",   "-SIGABRT",
    "-BUS",    "-SIGBUS",    "-FPE",    "-SIGFPE",    "-KILL",   "-SIGKILL",
    "-USR1",   "-SIGUSR1",   "-SEGV",   "-SIGSEGV",   "-USR2",   "-SIGUSR2",
    "-PIPE",   "-SIGPIPE",   "-ALRM",   "-SIGALRM",   "-TERM",   "-SIGTERM",
    "-STKFLT", "-SIGSTKFLT", "-CHLD",   "-SIGCHLD",   "-CLD",    "-SIGCLD",
    "-CONT",   "-SIGCONT",   "-STOP",   "-SIGSTOP",   "-TSTP",   "-SIGTSTP",
    "-TTIN",   "-SIGTTIN",   "-TTOU",   "-SIGTTOU",   "-URG",    "-SIGURG",
    "-XCPU",   "-SIGXCPU",   "-XFSZ",   "-SIGXFSZ",   "-VTALRM", "-SIGVTALRM",
    "-PROF",   "-SIGPROF",   "-WINCH",  "-SIGWINCH",  "-IO",     "-SIGIO",
    "-POLL",   "-SIGPOLL",   "-LOST",   "-SIGLOST",   "-PWR",    "-SIGPWR",
    "-SYS",    "-SIGSYS",    "-UNUSED", "-SIGUNUSED", "-IOT",    "-SIGIOT",
    "-EXIT",   "-SIGEXIT",   "-NULL",   "-SIGNULL",   "-RTMIN",  "-SIGRTMIN",
    "-RTMAX",  "-SIGRTMAX",
}};

// ===========================================================================
// Selection criteria (procps pgrep.c select_procs)
// ===========================================================================

// Split a comma-separated option list the way pgrep.c split_list()
// does: empty elements are invalid, no whitespace trimming.
inline auto split_list(std::string_view value)
    -> std::optional<std::vector<std::string>> {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t comma = value.find(',', start);
    auto item = value.substr(
        start, comma == std::string_view::npos ? comma : comma - start);
    if (item.empty()) return std::nullopt;
    out.emplace_back(item);
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return out;
}

// Strict unsigned list conversion (pgrep.c conv_pgrp/conv_sid style:
// the whole element must be numeric).
inline auto parse_ulong_list(std::string_view value,
                             std::vector<unsigned long>& out) -> bool {
  auto items = split_list(value);
  if (!items) return false;
  for (const auto& item : *items) {
    if (!all_digits(item)) return false;
    unsigned long long parsed = 0;
    for (char c : item) {
      parsed = parsed * 10 + static_cast<unsigned>(c - '0');
      if (parsed > 0xFFFFFFFFull) return false;
    }
    out.push_back(static_cast<unsigned long>(parsed));
  }
  return true;
}

// Validate account specs (pgrep.c conv_uid/conv_gid): a spec is valid
// when it is a bare number or names an account that resolves. The
// offending element is reported through bad_spec.
inline auto validate_account_specs(const std::vector<std::string>& specs,
                                   std::string& bad_spec) -> bool {
  for (const auto& spec : specs) {
    if (all_digits(spec)) continue;
    if (win32_lookup_account(utf8_to_wstring(spec))) continue;
    bad_spec = spec;
    return false;
  }
  return true;
}

inline auto account_matches_any(const std::vector<std::string>& specs,
                                const Win32AccountInfo& account) -> bool {
  for (const auto& spec : specs) {
    if (win32_account_matches(spec, account.name, account.id)) return true;
  }
  return false;
}

struct Criteria {
  // Pattern (optional upstream when at least one criterion is given).
  bool has_pattern = false;
  std::string pattern_text;  // as given, for the long-pattern warning
  std::optional<std::wregex> pattern;
  bool full = false;  // -f: match against the full command line

  // Selectors. Empty means "not given".
  std::vector<std::string> euid_specs;   // -u (token owner on Windows)
  std::vector<std::string> ruid_specs;   // -U (token owner on Windows)
  std::vector<std::string> group_specs;  // -G (token primary group)
  std::vector<unsigned long> ppids;      // -P
  std::vector<unsigned long> sessions;   // -s (0 already mapped to own)
  bool has_pidfile_pid = false;          // -F
  unsigned long pidfile_pid = 0;

  // Age / recency selection.
  bool newest = false;            // -n
  bool oldest = false;            // -o
  long older_than = -1;           // -O seconds; -1 = not given
  bool ignore_ancestors = false;  // -A
  bool negate = false;            // -v (pgrep only)

  auto has_criteria() const -> bool {
    return has_pattern || !euid_specs.empty() || !ruid_specs.empty() ||
           !group_specs.empty() || !ppids.empty() || !sessions.empty() ||
           has_pidfile_pid || newest || oldest || older_than >= 0;
  }
};

struct Selected {
  const Win32ProcessInfo* proc = nullptr;
  unsigned long long create_filetime = 0;  // 100ns units since 1601
};

// Compile the pattern with upstream -x anchoring (pgrep.c do_regcomp
// wraps the pattern in ^(...)$ under -x). ECMAScript grammar stands in
// for POSIX ERE in this codebase.
inline auto compile_pattern(const std::wstring& pattern, bool exact,
                            bool ignore_case) -> std::optional<std::wregex> {
  try {
    auto flags = std::regex_constants::ECMAScript;
    if (ignore_case) flags |= std::regex_constants::icase;
    std::wstring re_pattern = pattern;
    if (exact) re_pattern = L"^(?:" + re_pattern + L")$";
    return std::wregex(re_pattern, flags);
  } catch (const std::regex_error&) {
    return std::nullopt;
  }
}

// pgrep.c is_long_match(): the "longer than 15 characters" warning for
// fruitless plain-name searches.
inline auto is_long_match(std::string_view pattern) -> bool {
  if (pattern.size() <= 15) return false;
  return pattern.find('|') == std::string_view::npos &&
         pattern.find('[') == std::string_view::npos;
}

// Read and validate a pidfile the way pgrep.c read_pidfile() does:
// the file must be a regular file of size >= 1; with require_lock it
// must currently hold a lock; the first 11 bytes must start with a
// pid >= 1 followed by whitespace or end of data. "-" reads stdin.
// Returns false for every "pidfile not valid" case.
inline auto read_pidfile(std::string_view path, bool require_lock,
                         unsigned long& pid_out) -> bool {
  std::string content;
  if (path == "-") {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) return false;
    char buffer[256];
    DWORD read = 0;
    while (ReadFile(input, buffer, sizeof(buffer), &read, nullptr) &&
           read > 0) {
      content.append(buffer, read);
      if (content.size() > 4096) break;
    }
  } else {
    const auto wpath = utf8_to_wstring(std::string(path));
    const DWORD attrs = GetFileAttributesW(wpath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      return false;
    }
    HANDLE raw =
        CreateFileW(wpath.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) return false;
    UniqueHandle file(raw);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 1) return false;
    if (require_lock) {
      // Probe: if an exclusive non-blocking lock can be taken, no lock
      // was held (flock/fcntl probe in read_pidfile). Approximation:
      // LockFileEx stands in for both Linux lock flavors.
      OVERLAPPED overlapped{};
      if (LockFileEx(file.get(),
                     LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                     MAXDWORD, MAXDWORD, &overlapped)) {
        UnlockFileEx(file.get(), 0, MAXDWORD, MAXDWORD, &overlapped);
        return false;
      }
      if (GetLastError() != ERROR_LOCK_VIOLATION &&
          GetLastError() != ERROR_SHARING_VIOLATION) {
        return false;
      }
    }
    char buffer[11];
    DWORD read = 0;
    if (!ReadFile(file.get(), buffer, sizeof(buffer), &read, nullptr) ||
        read < 1) {
      return false;
    }
    content.assign(buffer, read);
  }
  // strtoul semantics: leading whitespace and an optional sign are
  // consumed by strtoul; trailing junk other than whitespace fails.
  const char* begin = content.c_str();
  char* end = nullptr;
  const unsigned long pid = std::strtoul(begin, &end, 10);
  if (end <= begin || pid < 1) return false;
  if (*end != '\0' &&
      std::string_view(" \t\n\v\f\r").find(*end) == std::string_view::npos) {
    return false;
  }
  pid_out = pid;
  return true;
}

// Run the selection. processes must outlive the result. The predicate
// follows select_procs(): the caller's own process is always skipped,
// every given selector must hold, the pattern (when given) must
// regex-search the name (or the full command line under -f), -A
// ancestors are dropped, and -v inverts the combined verdict before
// the newest/oldest reduction picks a single process by creation
// time (ties resolve to the largest pid for newest and the smallest
// for oldest, mirroring the upstream saved-pid comparisons).
inline auto select_processes(const Criteria& crit,
                             const std::vector<Win32ProcessInfo>& processes,
                             unsigned long long now_filetime)
    -> std::vector<Selected> {
  const DWORD self = GetCurrentProcessId();

  std::unordered_set<unsigned long> ancestors;
  if (crit.ignore_ancestors) {
    std::unordered_map<unsigned long, unsigned long> parent_of;
    for (const auto& proc : processes) parent_of[proc.pid] = proc.ppid;
    unsigned long cursor = 0;
    auto self_it = parent_of.find(self);
    if (self_it != parent_of.end()) cursor = self_it->second;
    for (int guard = 0; cursor != 0 && guard < 4096; ++guard) {
      if (!ancestors.insert(cursor).second) break;
      auto it = parent_of.find(cursor);
      if (it == parent_of.end()) break;
      cursor = it->second;
    }
  }

  std::vector<Selected> matched;
  for (const auto& proc : processes) {
    if (proc.pid == self || proc.pid == 0) continue;
    if (ancestors.contains(proc.pid)) continue;

    bool ok = true;
    if (ok && crit.has_pidfile_pid) {
      ok = static_cast<unsigned long>(proc.pid) == crit.pidfile_pid;
    }
    if (ok && !crit.ppids.empty()) {
      ok = std::ranges::any_of(crit.ppids, [&](unsigned long ppid) {
        return static_cast<unsigned long>(proc.ppid) == ppid;
      });
    }
    if (ok && !crit.sessions.empty()) {
      auto session = win32_process_session_id(proc.pid);
      ok = session.has_value() &&
           std::ranges::any_of(crit.sessions, [&](unsigned long wanted) {
             return static_cast<unsigned long>(*session) == wanted;
           });
    }
    if (ok && (!crit.euid_specs.empty() || !crit.ruid_specs.empty())) {
      auto owner = win32_process_account(proc.pid);
      // Windows tokens carry a single user, so the real and effective
      // selectors both test the token owner (procps -u/-U distinction
      // has no separate source here).
      ok = owner.has_value() &&
           (crit.euid_specs.empty() ||
            account_matches_any(crit.euid_specs, *owner)) &&
           (crit.ruid_specs.empty() ||
            account_matches_any(crit.ruid_specs, *owner));
    }
    if (ok && !crit.group_specs.empty()) {
      auto group = win32_process_primary_group(proc.pid);
      ok = group.has_value() && account_matches_any(crit.group_specs, *group);
    }

    std::optional<unsigned long long> create_ft;
    auto creation = [&]() -> unsigned long long {
      if (!create_ft) {
        create_ft = win32_process_creation_filetime(proc.pid).value_or(0);
      }
      return *create_ft;
    };
    if (ok && crit.older_than >= 0) {
      const double elapsed =
          now_filetime > creation()
              ? static_cast<double>(now_filetime - creation()) / 1.0e7
              : 0.0;
      // pgrep.c: (int)ELAPSED < opt_older fails the candidate.
      ok = static_cast<long>(elapsed) >= crit.older_than;
    }
    if (ok && crit.has_pattern) {
      std::wstring haystack = crit.full ? proc.command_line : proc.name;
      if (!crit.full) haystack = win32_basename_without_exe(haystack);
      ok = crit.pattern.has_value() &&
           std::regex_search(haystack, *crit.pattern);
    }
    if (crit.negate) ok = !ok;
    if (!ok) continue;

    Selected selected;
    selected.proc = &proc;
    if (crit.newest || crit.oldest) selected.create_filetime = creation();
    matched.push_back(selected);
  }

  if ((crit.newest || crit.oldest) && matched.size() > 1) {
    const Selected* best = &matched.front();
    for (const auto& candidate : matched) {
      if (crit.newest) {
        // select_procs(): on equal start times the saved entry is only
        // kept when its pid is the larger one, so ties resolve to the
        // largest pid; for oldest they resolve to the smallest pid.
        if (candidate.create_filetime > best->create_filetime ||
            (candidate.create_filetime == best->create_filetime &&
             candidate.proc->pid > best->proc->pid)) {
          best = &candidate;
        }
      } else {
        if (candidate.create_filetime < best->create_filetime ||
            (candidate.create_filetime == best->create_filetime &&
             candidate.proc->pid < best->proc->pid)) {
          best = &candidate;
        }
      }
    }
    Selected only = *best;
    matched.clear();
    matched.push_back(only);
  }
  return matched;
}

// Current time as a FILETIME tick count, for age computations.
inline auto now_filetime() -> unsigned long long {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

}  // namespace procps_select
