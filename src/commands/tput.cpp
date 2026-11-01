/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
#include "terminfo_table.h"
import std;
import core;
import utils;
import container;

namespace {

using namespace terminfo_table;


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

// [GNU] tput writes numeric capability answers with a trailing newline:
// `tput colors cols` → "256\n80\n". An absent capability ("-1") still exits 0.
void write_numeric(int value) {
  write_cap(std::to_string(value));
  write_cap("\n");
}

// [GNU] When stdout is a real console, ncurses overrides the terminfo
// cols/lines constants with the ioctl-queried size. GetConsoleScreenBufferInfo
// is the Windows equivalent; on failure (pipe, redirected file) the static
// terminfo values stand, matching the non-tty oracle.
void apply_console_size(TermEntry& entry) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
    entry.num_cols = info.srWindow.Right - info.srWindow.Left + 1;
    entry.num_lines = info.srWindow.Bottom - info.srWindow.Top + 1;
  }
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
    // [GNU] tput with no capname prints its usage on stderr and exits 2.
    safeErrorPrintLn(
        "Usage: tput [options] [command]\n"
        "\n"
        "Options:\n"
        "  -S <<       read commands from standard input\n"
        "  -T TERM     use this instead of $TERM\n"
        "  -V          print curses-version\n"
        "  -v          verbose, show warnings\n"
        "  -x          do not try to clear scrollback\n"
        "\n"
        "Commands:\n"
        "  clear       clear the screen\n"
        "  init        initialize the terminal\n"
        "  reset       reinitialize the terminal\n"
        "  capname     unlike clear/init/reset, print value for capability "
        "\"capname\"");
    return 2;
  }

  // [GNU] -T overrides $TERM; a missing/empty TERM is a usage-class error.
  const char* term_env = std::getenv("TERM");
  const std::string term = ctx.get<std::string>(
      "-T", std::string(term_env != nullptr ? term_env : ""));
  if (term.empty()) {
    safeErrorPrintLn("tput: No value for $TERM and no -T specified");
    return 2;
  }
  TermEntry entry = *lookup_term(term);
  const bool has_entries = entry.caps != nullptr;
  apply_console_size(entry);

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
  const auto numeric_cap = [](std::string_view cap,
                              const TermEntry& e) -> const int* {
    if (cap == "colors") return &e.num_colors;
    if (cap == "pairs") return &e.num_pairs;
    if (cap == "cols") return &e.num_cols;
    if (cap == "lines") return &e.num_lines;
    return nullptr;
  };

  size_t i = 0;
  while (i < ctx.positionals.size()) {
    const std::string_view cap = ctx.positionals[i];

    // [GNU] Numeric capabilities never error on an absent value: they print
    // it ("−1" when the entry lacks the capability) and exit 0.
    if (const int* value = numeric_cap(cap, entry)) {
      write_numeric(*value);
      ++i;
      continue;
    }

    const bool parameterized =
        cap == "setaf" || cap == "setab" || cap == "cup";
    const auto member = no_param_cap(cap);
    if (!parameterized && member == nullptr && cap != "reset") {
      // [GNU] unknown terminfo capability: diagnose, stop, exit 4.
      safeErrorPrintLn("tput: unknown terminfo capability '" +
                       std::string(cap) + "'");
      return 4;
    }

    // A recognized capability the terminfo entry lacks (vt100 civis, linux
    // smcup, every string capability on dumb) prints nothing and exits 1 —
    // the ncurses absent-capability contract.
    if (!has_entries) return 1;

    if (member != nullptr) {
      const char* slot = entry.caps->*member;
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

    // [GNU] Parameterized capabilities consume only leading NUMERIC operands:
    // `cup abc` formats with 0 and lets the loop diagnose 'abc' as the next
    // capname (rc 4); a missing operand entirely prints the raw template.
    const size_t max_operands = cap == "cup" ? 2 : 1;
    long operands[2] = {0, 0};
    size_t consumed = 1;
    while (consumed <= max_operands &&
           i + consumed < ctx.positionals.size()) {
      const auto v = parse_operand(ctx.positionals[i + consumed]);
      if (!v) break;
      operands[consumed - 1] = *v;
      ++consumed;
    }

    // No following argv at all → the raw template. A following non-numeric
    // argv stays formatted-with-0 (it will be diagnosed as the next capname).
    const bool has_next = i + 1 < ctx.positionals.size();

    if (cap == "cup") {
      if (consumed == 1 && !has_next) {
        write_cap(kCupTemplate);
        ++i;
        continue;
      }
      // tparm %i makes cup 1-based.
      write_cap("\x1b[" + std::to_string(operands[0] + 1) + ";" +
                std::to_string(operands[1] + 1) + "H");
      i += consumed;
      continue;
    }

    // setaf/setab.
    if (consumed == 1 && !has_next) {
      write_cap(cap == "setaf" ? kSetafTemplate : kSetabTemplate);
      ++i;
      continue;
    }
    if (entry.colors == Colors::kNone) return 1;
    write_cap(format_color(cap, entry.colors, operands[0]));
    i += consumed;
  }
  return 0;
}
