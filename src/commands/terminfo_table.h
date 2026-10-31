// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Shared builtin terminfo table for the ncurses command
///     family. Windows has no terminfo database, so tput and infocmp both
///     answer from this one oracle-verified subset instead of each keeping
///     a private (and divergent) copy. The capability values were verified
///     byte-for-byte against the WSL oracle (Ubuntu 24.04, ncurses 6.4)
///     for WinuxCmd#1460; keep them byte-identical when editing.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#pragma once

#include "pch/pch.h"

namespace terminfo_table {

// [GNU ncurses] One terminal's no-parameter capability strings, verified
// byte-for-byte against the WSL oracle (Ubuntu 24.04, ncurses 6.4 — the
// same table rubash#884's inline arm carries). nullptr = the capability is
// absent for that terminal: ncurses tput prints nothing and exits 1.
struct TermCaps {
  const char* bold;
  const char* sgr0;
  const char* clear;
  const char* civis;
  const char* cnorm;
  const char* el;
  const char* ed;
  const char* smcup;
  const char* rmcup;
};

// The setaf/setab parameter families, per the oracle:
// - Xterm8 (8-color entries xterm/screen/linux): N formats verbatim into
//   the termcap fallback "\e[3%p1%dm"/"\e[4%p1%dm" — setaf 10 = "\e[310m".
// - Colors256 (xterm-256color/screen-256color/tmux-256color): N < 8 →
//   "\e[3Nm"/"\e[4Nm"; 8-15 → the bright pair "\e[9Nm"/"\e[10Nm" (no ";5;"
//   palette segment — setaf 9 = "\e[91m"); N >= 16 → "\e[38;5;Nm"/
//   "\e[48;5;Nm".
enum class Colors { kNone, kXterm8, kColors256 };

inline constexpr TermCaps kXtermCaps{"\x1b[1m",
                                     "\x1b(B\x1b[m",
                                     "\x1b[H\x1b[2J\x1b[3J",
                                     "\x1b[?25l",
                                     "\x1b[?12l\x1b[?25h",
                                     "\x1b[K",
                                     "\x1b[J",
                                     "\x1b[?1049h\x1b[22;0;0t",
                                     "\x1b[?1049l\x1b[23;0;0t"};

inline constexpr TermCaps kScreenCaps{"\x1b[1m",
                                      "\x1b[m\x0f",
                                      "\x1b[H\x1b[J",
                                      "\x1b[?25l",
                                      "\x1b[34h\x1b[?25h",
                                      "\x1b[K",
                                      "\x1b[J",
                                      "\x1b[?1049h",
                                      "\x1b[?1049l"};

// tmux-256color's clear carries E3 where plain screen's does not — the
// one string that differs from kScreenCaps.
inline constexpr TermCaps kTmuxCaps{"\x1b[1m",
                                    "\x1b[m\x0f",
                                    "\x1b[H\x1b[J\x1b[3J",
                                    "\x1b[?25l",
                                    "\x1b[34h\x1b[?25h",
                                    "\x1b[K",
                                    "\x1b[J",
                                    "\x1b[?1049h",
                                    "\x1b[?1049l"};

inline constexpr TermCaps kLinuxCaps{"\x1b[1m",
                                     "\x1b[m\x0f",
                                     "\x1b[H\x1b[J\x1b[3J",
                                     "\x1b[?25l\x1b[?1c",
                                     "\x1b[?25h\x1b[?0c",
                                     "\x1b[K",
                                     "\x1b[J",
                                     nullptr,
                                     nullptr};

inline constexpr TermCaps kVt100Caps{"\x1b[1m",
                                     "\x1b[m\x0f",
                                     "\x1b[H\x1b[J",
                                     nullptr,
                                     nullptr,
                                     "\x1b[K",
                                     "\x1b[J",
                                     nullptr,
                                     nullptr};

struct TermEntry {
  const TermCaps* caps;  // nullptr = every string capability absent (dumb)
  Colors colors;
  // Numeric capabilities, oracle-verified on Ubuntu 24.04 / ncurses 6.4 in
  // a non-tty context (WinuxCmd#1460). -1 = the capability is absent:
  // ncurses prints "-1" and exits 0, it does NOT error.
  int num_colors;
  int num_pairs;
  int num_cols;
  int num_lines;
};

inline constexpr TermEntry kXtermEntry{
    &kXtermCaps, Colors::kXterm8, 8, 64, 80, 24};
inline constexpr TermEntry kXterm256Entry{
    &kXtermCaps, Colors::kColors256, 256, 65536, 80, 24};
inline constexpr TermEntry kScreenEntry{
    &kScreenCaps, Colors::kXterm8, 8, 64, 80, 24};
inline constexpr TermEntry kScreen256Entry{
    &kScreenCaps, Colors::kColors256, 256, 65536, 80, 24};
inline constexpr TermEntry kTmuxEntry{
    &kTmuxCaps, Colors::kColors256, 256, 65536, 80, 24};
inline constexpr TermEntry kLinuxEntry{
    &kLinuxCaps, Colors::kXterm8, 8, 64, -1, -1};
inline constexpr TermEntry kVt100Entry{
    &kVt100Caps, Colors::kNone, -1, -1, 80, 24};
inline constexpr TermEntry kDumbEntry{nullptr, Colors::kNone, -1, -1, 80, -1};

// [GNU] Raw terminfo parameterized templates, byte-equal to the oracle.
// tput prints a parameterized capability's template verbatim when its
// operand is missing entirely (`tput setaf` with nothing after it).
inline constexpr char kSetafTemplate[] =
    "\x1b[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m";
inline constexpr char kSetabTemplate[] =
    "\x1b[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m";
inline constexpr char kCupTemplate[] = "\x1b[%i%p1%d;%p2%dH";

// One named builtin entry: the canonical terminfo name, the description
// infocmp prints after it, and the verified capability data.
struct NamedTerm {
  const char* name;
  const char* description;
  const TermEntry* entry;
};

inline constexpr NamedTerm kNamedTerms[] = {
    {"xterm", "xterm terminal emulator", &kXtermEntry},
    {"xterm-256color", "xterm with 256 colors", &kXterm256Entry},
    {"screen", "VT 100/ANSI X3.64 virtual terminal", &kScreenEntry},
    {"screen-256color", "screen with 256 colors", &kScreen256Entry},
    {"tmux-256color", "tmux with 256 colors", &kTmuxEntry},
    {"linux", "Linux console", &kLinuxEntry},
    {"vt100", "DEC VT100", &kVt100Entry},
    {"dumb", "dumb terminal", &kDumbEntry},
};

// Strict lookup: nullptr when the name is not a builtin entry. infocmp
// uses this — printing another terminal's entry under the requested name
// would be a false answer.
inline const NamedTerm* find_term(std::string_view term) {
  for (const auto& named : kNamedTerms) {
    if (term == named.name) return &named;
  }
  return nullptr;
}

// [GNU] tput answers capability queries from the terminfo entry named by
// TERM (or -T). This is the oracle-verified subset; an unlisted TERM
// falls back to the xterm family — the historical pre-TERM behavior
// emitted ANSI sequences unconditionally, and nearly every other common
// Windows-side TERM value (ansi, cygwin, alacritty, wezterm…) is
// xterm-flavored.
inline const TermEntry* lookup_term(std::string_view term) {
  if (const NamedTerm* named = find_term(term)) return named->entry;
  return &kXtermEntry;
}

}  // namespace terminfo_table
