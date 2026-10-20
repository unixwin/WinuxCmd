/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

namespace {

// [GNU ncurses] One terminal's no-parameter capability strings, verified
// byte-for-byte against the WSL oracle (Ubuntu 24.04, ncurses 6.4 — the same
// table rubash#884's inline arm carries). nullptr = the capability is absent
// for that terminal: ncurses tput prints nothing and exits 1.
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
// - Xterm8 (8-color entries xterm/screen/linux): N formats verbatim into the
//   termcap fallback "\e[3%p1%dm"/"\e[4%p1%dm" — setaf 10 = "\e[310m".
// - Colors256 (xterm-256color/screen-256color/tmux-256color): N < 8 →
//   "\e[3Nm"/"\e[4Nm"; 8-15 → the bright pair "\e[9Nm"/"\e[10Nm" (no ";5;"
//   palette segment — setaf 9 = "\e[91m"); N >= 16 → "\e[38;5;Nm"/"\e[48;5;Nm".
enum class Colors { kNone, kXterm8, kColors256 };

constexpr TermCaps kXtermCaps{"\x1b[1m",
                              "\x1b(B\x1b[m",
                              "\x1b[H\x1b[2J\x1b[3J",
                              "\x1b[?25l",
                              "\x1b[?12l\x1b[?25h",
                              "\x1b[K",
                              "\x1b[J",
                              "\x1b[?1049h\x1b[22;0;0t",
                              "\x1b[?1049l\x1b[23;0;0t"};

constexpr TermCaps kScreenCaps{"\x1b[1m",
                               "\x1b[m\x0f",
                               "\x1b[H\x1b[J",
                               "\x1b[?25l",
                               "\x1b[34h\x1b[?25h",
                               "\x1b[K",
                               "\x1b[J",
                               "\x1b[?1049h",
                               "\x1b[?1049l"};

// tmux-256color's clear carries E3 where plain screen's does not — the one
// string that differs from kScreenCaps.
constexpr TermCaps kTmuxCaps{"\x1b[1m",
                             "\x1b[m\x0f",
                             "\x1b[H\x1b[J\x1b[3J",
                             "\x1b[?25l",
                             "\x1b[34h\x1b[?25h",
                             "\x1b[K",
                             "\x1b[J",
                             "\x1b[?1049h",
                             "\x1b[?1049l"};

constexpr TermCaps kLinuxCaps{"\x1b[1m",
                              "\x1b[m\x0f",
                              "\x1b[H\x1b[J\x1b[3J",
                              "\x1b[?25l\x1b[?1c",
                              "\x1b[?25h\x1b[?0c",
                              "\x1b[K",
                              "\x1b[J",
                              nullptr,
                              nullptr};

constexpr TermCaps kVt100Caps{"\x1b[1m",
                              "\x1b[m\x0f",
                              "\x1b[H\x1b[J",
                              nullptr,
                              nullptr,
                              "\x1b[K",
                              "\x1b[J",
                              nullptr,
                              nullptr};

struct TermEntry {
  const TermCaps* caps;  // nullptr = every capability absent (TERM=dumb)
  Colors colors;
};

constexpr TermEntry kXtermEntry{&kXtermCaps, Colors::kXterm8};
constexpr TermEntry kXterm256Entry{&kXtermCaps, Colors::kColors256};
constexpr TermEntry kScreenEntry{&kScreenCaps, Colors::kXterm8};
constexpr TermEntry kScreen256Entry{&kScreenCaps, Colors::kColors256};
constexpr TermEntry kTmuxEntry{&kTmuxCaps, Colors::kColors256};
constexpr TermEntry kLinuxEntry{&kLinuxCaps, Colors::kXterm8};
constexpr TermEntry kVt100Entry{&kVt100Caps, Colors::kNone};
constexpr TermEntry kDumbEntry{nullptr, Colors::kNone};

// [GNU] tput answers capability queries from the terminfo entry named by
// TERM (or -T). This is the oracle-verified subset; an unlisted TERM falls
// back to the xterm family — the historical pre-TERM behavior emitted ANSI
// sequences unconditionally, and nearly every other common Windows-side
// TERM value (ansi, cygwin, alacritty, wezterm…) is xterm-flavored.
const TermEntry* lookup_term(std::string_view term) {
  if (term == "xterm") return &kXtermEntry;
  if (term == "xterm-256color") return &kXterm256Entry;
  if (term == "screen") return &kScreenEntry;
  if (term == "screen-256color") return &kScreen256Entry;
  if (term == "tmux-256color") return &kTmuxEntry;
  if (term == "linux") return &kLinuxEntry;
  if (term == "vt100") return &kVt100Entry;
  if (term == "dumb") return &kDumbEntry;
  return &kXtermEntry;
}

// Capability strings are terminal protocol bytes, not natural language:
// they bypass i18n translation and are written verbatim. Routing them
// through safePrint's translate path used to pay the lazy catalog read+parse
// on every call when a WINUX_LANG catalog was installed (#1258 ~17 ms).
void write_cap(std::string_view bytes) {
  if (bytes.empty()) return;
  DWORD written = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(),
            static_cast<DWORD>(bytes.size()), &written, nullptr);
}

// [GNU] ncurses tput parses numeric operands with strtol(arg, NULL, 0):
// "010" is octal 8, "0x10" hexadecimal 16, "009" truncates at the first
// illegal octal digit, "+7" is a signed 7. Calling the same libc function
// keeps every operand spelling byte-equal with the oracle.
std::optional<long> parse_operand(std::string_view arg) {
  if (arg.empty()) return std::nullopt;
  std::string text(arg);
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 0);
  if (end == text.c_str()) return std::nullopt;
  return value;
}

std::string format_color(std::string_view cap, Colors colors, long n) {
  std::string out("\x1b[");
  if (colors == Colors::kXterm8) {
    out += cap == "setaf" ? '3' : '4';
    out += std::to_string(n);
  } else if (n < 8) {
    out += cap == "setaf" ? '3' : '4';
    out += std::to_string(n);
  } else if (n <= 15) {
    out += std::to_string((cap == "setaf" ? 90 : 100) + n - 8);
  } else {
    out += cap == "setaf" ? "38;5;" : "48;5;";
    out += std::to_string(n);
  }
  out += 'm';
  return out;
}

}  // namespace

auto constexpr TPUT_OPTIONS =
    // [GNU] option
    std::array{OPTION("-T", "", "specify terminal type", STRING_TYPE),
               // [EXT] operand
               OPTION("", "", "terminal capabilities", STRING_TYPE)};

REGISTER_COMMAND(tput,
                 /* cmd_name */ "tput",
                 /* cmd_synopsis */ "tput [CAPNAME]",
                 /* cmd_desc */ "Initialize a terminal or query terminfo.",
                 /* examples */ "tput clear",
                 /* see_also */ "reset",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TPUT_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("tput: missing capability name");
    return 1;
  }

  // [GNU] -T overrides $TERM; a missing/empty TERM is a usage-class error.
  const char* term_env = std::getenv("TERM");
  const std::string term = ctx.get<std::string>(
      "-T", std::string(term_env != nullptr ? term_env : ""));
  if (term.empty()) {
    safeErrorPrintLn("tput: No value for $TERM and no -T specified");
    return 2;
  }
  const TermEntry* entry = lookup_term(term);
  const bool has_entries = entry->caps != nullptr;

  // GNU consumes argv as a stream: each capname is followed by the operands
  // its capability takes, so `tput cup 5 10 clear` emits both strings.
  const auto no_param_cap = [](std::string_view cap) -> const char* TermCaps::* {
    if (cap == "bold") return &TermCaps::bold;
    if (cap == "sgr0") return &TermCaps::sgr0;
    if (cap == "clear") return &TermCaps::clear;
    if (cap == "el") return &TermCaps::el;
    if (cap == "ed") return &TermCaps::ed;
    if (cap == "civis") return &TermCaps::civis;
    if (cap == "cnorm") return &TermCaps::cnorm;
    if (cap == "smcup") return &TermCaps::smcup;
    if (cap == "rmcup") return &TermCaps::rmcup;
    return nullptr;
  };

  size_t i = 0;
  while (i < ctx.positionals.size()) {
    const std::string_view cap = ctx.positionals[i];
    const bool parameterized =
        cap == "setaf" || cap == "setab" || cap == "cup";
    const auto member = no_param_cap(cap);
    if (!parameterized && member == nullptr && cap != "reset") {
      safeErrorPrintLn("tput: unknown capability '" + std::string(cap) + "'");
      return 1;
    }

    // A recognized capability the terminfo entry lacks (vt100 civis, linux
    // smcup, every capability on dumb) prints nothing and exits 1 — the
    // ncurses absent-capability contract.
    if (!has_entries) return 1;

    if (member != nullptr) {
      const char* slot = entry->caps->*member;
      if (slot == nullptr) return 1;
      write_cap(slot);
      ++i;
      continue;
    }

    if (cap == "reset") {
      // [EXT] `tput reset` predates the terminfo table; keep the historical
      // RIS answer.
      write_cap("\x1b" "c");
      ++i;
      continue;
    }

    if (cap == "cup") {
      // [GNU] `cup Y X` → "\e[{Y+1};{X+1}H" (tparm %i, 1-based). cup pops
      // exactly two operands: a missing one pads to 0 (`cup 3` → "\e[4;1H"),
      // a present non-numeric one is an error.
      const auto row = i + 1 < ctx.positionals.size()
                           ? parse_operand(ctx.positionals[i + 1])
                           : std::nullopt;
      if (!row) break;
      long column = 0;
      size_t consumed = 2;
      if (i + 2 < ctx.positionals.size()) {
        const auto c = parse_operand(ctx.positionals[i + 2]);
        if (!c) break;
        column = *c;
        consumed = 3;
      }
      write_cap("\x1b[" + std::to_string(*row + 1) + ";" +
                std::to_string(column + 1) + "H");
      i += consumed;
      continue;
    }

    // setaf/setab: one numeric operand.
    const auto n = i + 1 < ctx.positionals.size()
                       ? parse_operand(ctx.positionals[i + 1])
                       : std::nullopt;
    if (!n) break;
    if (entry->colors == Colors::kNone) return 1;
    write_cap(format_color(cap, entry->colors, *n));
    i += 2;
  }

  // A recognized parameterized capability whose operands are missing or
  // non-numeric is GNU's rc-4 diagnostic surface; report it as a failed
  // invocation rather than silently succeeding.
  if (i < ctx.positionals.size()) {
    safeErrorPrintLn("tput: unknown capability '" +
                     std::string(ctx.positionals[i]) + "'");
    return 1;
  }
  return 0;
}
