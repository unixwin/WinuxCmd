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

using namespace terminfo_table;

namespace {

// One dumpable builtin entry, capabilities already in the canonical
// terminfo order GNU infocmp prints (booleans, then numbers, then
// strings; each group in terminfo(5) table order). Only capabilities the
// product has actually verified are listed — an infocmp dump that
// invents capabilities would be worse than a short one.
struct DumpEntry {
  const char* name;
  const char* description;
  std::vector<const char*> bools;
  std::vector<std::pair<const char*, int>> nums;
  std::vector<std::pair<const char*, std::string>> strs;  // raw bytes
};

// The product's own console entry, carried over from the historical
// hardcoded dump: these are the capabilities WinuxCmd has always claimed
// for the Windows console, kept byte-for-byte.
DumpEntry windows_ansi_entry() {
  DumpEntry e;
  e.name = "windows-ansi";
  e.description = "Windows ANSI Terminal";
  e.bools = {"am"};
  e.nums = {{"cols", 80}, {"it", 8}, {"lines", 24}};
  e.strs = {{"cr", "\r"},
            {"clear", "\x1b[H\x1b[2J"},
            {"smso", "\x1b[7m"},
            {"smul", "\x1b[4m"},
            {"sgr0", "\x1b[0m"}};
  return e;
}

DumpEntry entry_from_named(const NamedTerm& named) {
  DumpEntry e;
  e.name = named.name;
  e.description = named.description;
  const TermEntry& t = *named.entry;
  if (t.num_cols >= 0) e.nums.emplace_back("cols", t.num_cols);
  if (t.num_lines >= 0) e.nums.emplace_back("lines", t.num_lines);
  if (t.num_colors >= 0) e.nums.emplace_back("colors", t.num_colors);
  if (t.num_pairs >= 0) e.nums.emplace_back("pairs", t.num_pairs);
  if (t.caps != nullptr) {
    const TermCaps& c = *t.caps;
    // Canonical string order (terminfo(5)): clear, el, ed, cup, civis,
    // cnorm, bold, smcup, sgr0, rmcup, then the color pair setab/setaf.
    if (c.clear != nullptr) e.strs.emplace_back("clear", c.clear);
    if (c.el != nullptr) e.strs.emplace_back("el", c.el);
    if (c.ed != nullptr) e.strs.emplace_back("ed", c.ed);
    e.strs.emplace_back("cup", kCupTemplate);
    if (c.civis != nullptr) e.strs.emplace_back("civis", c.civis);
    if (c.cnorm != nullptr) e.strs.emplace_back("cnorm", c.cnorm);
    if (c.bold != nullptr) e.strs.emplace_back("bold", c.bold);
    if (c.smcup != nullptr) e.strs.emplace_back("smcup", c.smcup);
    if (c.sgr0 != nullptr) e.strs.emplace_back("sgr0", c.sgr0);
    if (c.rmcup != nullptr) e.strs.emplace_back("rmcup", c.rmcup);
    if (t.colors != Colors::kNone) {
      e.strs.emplace_back("setab", kSetabTemplate);
      e.strs.emplace_back("setaf", kSetafTemplate);
    }
  }
  return e;
}

// [GNU ncurses] _nc_tic_expand escaping, as used for infocmp source
// dumps: ESC is \E, the named control escapes keep their two-letter
// forms, other control bytes are ^X, DEL is ^?.
std::string expand_bytes(std::string_view raw) {
  std::string out;
  for (const unsigned char ch : raw) {
    switch (ch) {
      case 0x1b:
        out += "\\E";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case 0x7f:
        out += "^?";
        break;
      default:
        if (ch < 0x20) {
          out += '^';
          out += static_cast<char>(ch + '@');
        } else {
          out += static_cast<char>(ch);
        }
    }
  }
  return out;
}

std::string render(const DumpEntry& e, bool one_per_line) {
  std::vector<std::string> caps;
  for (const char* b : e.bools) caps.emplace_back(b);
  for (const auto& [name, value] : e.nums) {
    caps.push_back(std::string(name) + "#" + std::to_string(value));
  }
  for (const auto& [name, raw] : e.strs) {
    caps.push_back(std::string(name) + "=" + expand_bytes(raw));
  }

  std::string out =
      "#\tReconstructed via infocmp from file: builtin terminfo table\n";
  out += e.name;
  out += "|";
  out += e.description;
  out += ",\n";

  if (one_per_line) {
    for (size_t i = 0; i < caps.size(); ++i) {
      out += "\t" + caps[i] + (i + 1 < caps.size() ? "," : "") + "\n";
    }
    return out;
  }

  // Wrapped form: tab indent, ", " between capabilities, wrap at
  // column 79 like the ncurses dump (dump_entry.c wrap_concat).
  std::string line = "\t";
  for (size_t i = 0; i < caps.size(); ++i) {
    std::string piece = caps[i] + (i + 1 < caps.size() ? "," : "");
    if (line.size() > 1 && line.size() + 1 + piece.size() > 79) {
      out += line + "\n";
      line = "\t";
    }
    if (line.size() > 1) line += " ";
    line += piece;
  }
  out += line + "\n";
  return out;
}

}  // namespace

auto constexpr INFOCMP_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "print capabilities one per line", BOOL_TYPE),
    // [EXT]
    OPTION("-V", "", "print curses-version", BOOL_TYPE),
    // [EXT] operand
    OPTION("", "", "print terminfo description for TERMNAME", STRING_TYPE)};

REGISTER_COMMAND(infocmp,
                 /* cmd_name */ "infocmp",
                 /* cmd_synopsis */ "infocmp [-1] [TERMNAME]",
                 /* cmd_desc */
                 "Print the terminfo description of a terminal from the "
                 "builtin terminfo table, in GNU infocmp source format.",
                 /* examples */ "infocmp\ninfocmp -1 xterm",
                 /* see_also */ "tput, toe, tic",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ INFOCMP_OPTIONS) {
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("ncurses 6.6.20251230");
    return 0;
  }

  if (ctx.positionals.size() > 1) {
    safeErrorPrintLn(winux::i18n::format(
        "command.infocmp.error.compare_unsupported",
        "infocmp: comparing multiple terminals is not supported on "
        "Windows (no terminfo database)"));
    return 1;
  }

  // [GNU] The entry dumped is the operand's, or $TERM's when no operand
  // is given (infocmp.c via setupterm). The historical build ignored
  // both and always printed the windows-ansi entry.
  std::string term;
  if (!ctx.positionals.empty()) {
    term = std::string(ctx.positionals.front());
  } else if (const char* env = std::getenv("TERM"); env != nullptr) {
    term = env;
  }
  if (term.empty()) {
    safeErrorPrintLn(winux::i18n::format(
        "command.infocmp.error.no_term",
        "infocmp: No value for $TERM and no terminal specified"));
    return 1;
  }

  const bool one_per_line = ctx.get<bool>("-1", false);
  if (term == "windows-ansi") {
    safePrint(render(windows_ansi_entry(), one_per_line));
    return 0;
  }
  // Strict lookup: unlike tput (which must always answer something),
  // infocmp printing a different terminal's entry under the requested
  // name would be a false answer, so unknown names take the GNU error:
  // "infocmp: couldn't open terminfo file %s." (infocmp.c).
  const NamedTerm* named = find_term(term);
  if (named == nullptr) {
    safeErrorPrintLn(winux::i18n::format(
        "command.infocmp.error.couldnt_open",
        "infocmp: couldn't open terminfo file {}.", term));
    return 1;
  }
  safePrint(render(entry_from_named(*named), one_per_line));
  return 0;
}
