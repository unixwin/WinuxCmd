// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for col.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr COL_OPTIONS = std::array{
    // [GNU] util-linux col.c longopts (col.c:530-541) and optstring
    // "bfhl:pxVH" (col.c:547).  Note -h is --tabs here, NOT help;
    // help is -H/--help.
    OPTION("-b", "--no-backspaces", "do not output backspaces", BOOL_TYPE),
    OPTION("-f", "--fine", "permit forward half line feeds", BOOL_TYPE),
    OPTION("-p", "--pass", "pass unknown control sequences", BOOL_TYPE),
    OPTION("-h", "--tabs", "convert spaces to tabs", BOOL_TYPE),
    OPTION("-x", "--spaces", "convert tabs to spaces", BOOL_TYPE),
    OPTION("-l", "--lines", "buffer at least NUM lines", STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit",
           BOOL_TYPE),
    OPTION("-H", "--help", "display this help and exit", BOOL_TYPE)};

namespace col_pipeline {
namespace cp = core::pipeline;

// One stored character on a half-line row (col.c:99-107 struct col_char,
// reduced to the byte model: width is wcwidth in the C locale, so 1 for
// printable bytes and -1 for control bytes passed through with -p).
struct ColChar {
  size_t column = 0;
  char ch = 0;
  int width = 1;
  bool alternate = false;  // CS_ALTERNATE (SO/SI, col.c:96-98,312-317)
};

struct Row {
  std::vector<ColChar> chars;
  size_t max_col = 0;
};

struct Config {
  bool no_backspaces = false;
  bool fine = false;
  bool pass_unknown = false;
  bool compress_spaces = true;  // col.c:649: default on; -x turns it off
  size_t max_bufd_lines = 64;   // half lines: BUFFER_MARGIN*2 (col.c:651)
};

// col.c:300 errtryhelp shape.
void print_try_help() {
  safeErrorPrintLn(winux::i18n::format(
      "common.try_help", "Try '{} --help' for more information.", "col"));
}

// strtou32_or_err (col.c:565): the whole string must be one unsigned
// 32-bit value (a leading '+' is accepted by strtoumax).
bool parse_lines_value(std::string_view text, size_t& half_lines) {
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return false;
  unsigned long long value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<unsigned long long>(c - '0');
    if (value > 0xFFFFFFFFull) return false;
  }
  half_lines = static_cast<size_t>(value) * 2;
  return true;
}

// getopt_long decides version/help/bad-option-argument in argv order
// (col.c:547-586); the set-based ctx binding cannot express that, so
// prescan ctx.raw_args exactly like the getopt fix does.  Returns 0 to
// continue with the normal run, or the exit code to return at once
// (with any output already produced).  GNU getopt permutes, so options
// are honoured even after operands; scanning stops at "--".
int prescan(const CommandContext<COL_OPTIONS.size()>& ctx, Config& cfg) {
  const auto& args = ctx.raw_args;
  bool seen_tabs = false;
  bool seen_spaces = false;
  auto exclusive_check = [&](bool tabs) -> int {
    if (tabs) {
      if (seen_spaces) {
        safeErrorPrintLn("col: mutually exclusive arguments: --tabs --spaces");
        return 1;
      }
      seen_tabs = true;
    } else {
      if (seen_tabs) {
        safeErrorPrintLn("col: mutually exclusive arguments: --tabs --spaces");
        return 1;
      }
      seen_spaces = true;
    }
    return 0;
  };
  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view tok = args[i];
    if (tok == "--") break;
    if (tok == "--version") {
      safePrintLn("col (WinuxCmd) 1.0");
      return 0;
    }
    if (tok == "--help") {
      return -2;  // caller prints usage (kept in one place)
    }
    if (tok == "--no-backspaces" || tok == "--fine" || tok == "--pass")
      continue;
    if (tok == "--tabs") {
      if (int rc = exclusive_check(true)) return rc;
      continue;
    }
    if (tok == "--spaces") {
      if (int rc = exclusive_check(false)) return rc;
      continue;
    }
    if (tok == "--lines") {
      if (i + 1 >= args.size()) return -1;  // framework reports it
      size_t v = 0;
      if (!parse_lines_value(args[i + 1], v)) {
        safeErrorPrintLn("col: bad -l argument: '" +
                         std::string(args[i + 1]) + "'");
        return 1;
      }
      cfg.max_bufd_lines = v;
      ++i;
      continue;
    }
    if (tok.starts_with("--lines=")) {
      size_t v = 0;
      if (!parse_lines_value(tok.substr(8), v)) {
        safeErrorPrintLn("col: bad -l argument: '" +
                         std::string(tok.substr(8)) + "'");
        return 1;
      }
      cfg.max_bufd_lines = v;
      continue;
    }
    if (tok.size() >= 2 && tok[0] == '-' && tok[1] != '-') {
      // Bundled short options, walked in order like getopt does.
      for (size_t j = 1; j < tok.size(); ++j) {
        char c = tok[j];
        if (c == 'V') {
          safePrintLn("col (WinuxCmd) 1.0");
          return 0;
        }
        if (c == 'H') return -2;
        if (c == 'h') {
          if (int rc = exclusive_check(true)) return rc;
        } else if (c == 'x') {
          if (int rc = exclusive_check(false)) return rc;
        } else if (c == 'l') {
          std::string_view val;
          if (j + 1 < tok.size()) {
            val = tok.substr(j + 1);
          } else if (i + 1 < args.size()) {
            val = args[++i];
          } else {
            return -1;  // framework reports the missing argument
          }
          size_t v = 0;
          if (!parse_lines_value(val, v)) {
            safeErrorPrintLn("col: bad -l argument: '" + std::string(val) +
                             "'");
            return 1;
          }
          cfg.max_bufd_lines = v;
          break;
        }
        // 'b', 'f', 'p' and unknown letters: nothing decisive; an
        // unknown option is the framework's error to report.
      }
      continue;
    }
    // Operand or unknown long option: operands are judged after the
    // whole parse (col.c:583-586), unknown longs by the framework.
  }
  return -1;
}

void print_usage() {
  safePrintLn("Usage:");
  safePrintLn(" col [options]");
  safePrintLn("");
  safePrintLn("Filter out reverse line feeds from standard input.");
  safePrintLn("");
  safePrintLn("Options:");
  safePrintLn(" -b, --no-backspaces    do not output backspaces");
  safePrintLn(" -f, --fine             permit forward half line feeds");
  safePrintLn(" -p, --pass             pass unknown control sequences");
  safePrintLn(" -h, --tabs             convert spaces to tabs");
  safePrintLn(" -x, --spaces           convert tabs to spaces");
  safePrintLn(" -l, --lines NUM        buffer at least NUM lines");
  safePrintLn(" -H, --help             display this help");
  safePrintLn(" -V, --version          display version");
}

// The filter itself, following col.c's half-line model: positions are
// counted in half lines (NL advances 2, col.c:428-433), rows are
// flushed in order with blank separation re-emitted by flush_blanks
// (col.c:203-228) and each row rendered by flush_line (col.c:236-339).
struct Filter {
  const Config& cfg;
  std::deque<Row> rows;
  long long origin = 0;        // half-line index of rows.front()
  long long node_line = 0;     // row the last stored character went to
  long long total_flushed = 0;
  long long nblank = 0;        // col.c nblank_lines, in half lines
  long long cur_line = 0;
  long long max_line = 0;
  long long cur_col = 0;
  bool cur_alt = false;
  bool last_alt = false;       // ctl.last_set, persists across rows
  bool have_last = false;
  int last_width = 1;          // width of the last stored char (BS, col.c:404)
  bool warned = false;
  std::string out;

  void flush_blanks() {
    long long nb = nblank;
    bool half = false;
    if (nb & 1) {
      if (cfg.fine)
        half = true;
      else
        ++nb;
    }
    nb /= 2;
    for (long long k = 0; k < nb; ++k) out.push_back('\n');
    if (half) {
      out.push_back('\033');
      out.push_back('9');
      if (!nb) out.push_back('\r');
    }
    nblank = 0;
  }

  void flush_line(Row& row) {
    std::stable_sort(row.chars.begin(), row.chars.end(),
                     [](const ColChar& a, const ColChar& b) {
                       return a.column < b.column;
                     });
    size_t last_col = 0;
    size_t i = 0;
    while (i < row.chars.size()) {
      size_t j = i;
      while (j < row.chars.size() && row.chars[j].column == row.chars[i].column)
        ++j;
      const size_t this_col = row.chars[i].column;
      if (last_col < this_col) {
        // Tabs and spaces handling (col.c:293-306): with compression
        // (the default), a gap of 2+ becomes tabs up to the tab stop
        // plus the remaining spaces.
        long long nspace = static_cast<long long>(this_col - last_col);
        if (cfg.compress_spaces && 1 < nspace) {
          long long ntabs =
              static_cast<long long>(this_col / 8 - last_col / 8);
          if (0 < ntabs) {
            nspace = static_cast<long long>(this_col & 7);
            while (0 <= --ntabs) out.push_back('\t');
          }
        }
        while (0 <= --nspace) out.push_back(' ');
        last_col = this_col;
      }
      if (cfg.no_backspaces) {
        // Print only the last character of the column group (col.c:282).
        const ColChar& c = row.chars[j - 1];
        emit_char(c);
        last_col += c.width > 0 ? static_cast<size_t>(c.width) : 0;
      } else {
        for (size_t k = i; k < j; ++k) {
          emit_char(row.chars[k]);
          if (k + 1 < j) {
            // Rubout the character just printed (col.c:327-331).
            for (int b = 0; b < row.chars[k].width; ++b) out.push_back('\b');
          }
        }
        const ColChar& last = row.chars[j - 1];
        last_col += last.width > 0 ? static_cast<size_t>(last.width) : 0;
      }
      i = j;
    }
  }

  void emit_char(const ColChar& c) {
    if (c.alternate != last_alt) {
      out.push_back(c.alternate ? '\016' : '\017');  // SO / SI (col.c:312-317)
      last_alt = c.alternate;
    }
    out.push_back(c.ch);
  }

  void flush_rows(long long count) {
    while (count-- > 0 && !rows.empty()) {
      Row row = std::move(rows.front());
      rows.pop_front();
      ++origin;
      ++total_flushed;
      if (!row.chars.empty()) {
        flush_blanks();
        flush_line(row);
      }
      ++nblank;
    }
  }

  // Bring the storage row for a printable character into the buffer,
  // mirroring update_cur_line (col.c:465-526): without -f an odd
  // (half-line) position rounds up to the next whole line.
  void ensure_store() {
    long long t = cur_line;
    if (!cfg.fine && (t & 1)) ++t;
    if (t < origin) {
      if (total_flushed == 0) {
        // Backing up past the first line is allowed before anything
        // has been flushed: prepend blank rows (col.c:489-499).
        while (origin > t) {
          rows.emplace_front();
          --origin;
        }
      } else {
        if (!warned) {
          safeErrorPrintLn(cur_line < 0
                               ? "col: warning: can't back up past first line."
                               : "col: warning: can't back up -- line already "
                                 "flushed.");
          warned = true;
        }
        t = node_line;
      }
    }
    while (t >= origin + static_cast<long long>(rows.size()))
      rows.emplace_back();
    node_line = t;
    // Buffer limit (col.c:519-524): flush all but max_bufd_lines rows
    // once the distance exceeds the limit plus BUFFER_MARGIN (32).
    long long nmove = node_line - origin;
    if (0 < nmove &&
        static_cast<long long>(cfg.max_bufd_lines) + 32 <= nmove)
      flush_rows(nmove - static_cast<long long>(cfg.max_bufd_lines));
  }

  void store(char ch, int width) {
    ensure_store();
    Row& row = rows[static_cast<size_t>(node_line - origin)];
    ColChar c;
    c.column = cur_col > 0 ? static_cast<size_t>(cur_col) : 0;
    c.ch = ch;
    c.width = width;
    c.alternate = cur_alt;
    if (c.column > row.max_col) row.max_col = c.column;
    row.chars.push_back(c);
    have_last = true;
    last_width = width;
    if (0 < width) cur_col += width;
  }

  void forward(long long delta) {
    cur_line += delta;
    if (0 < cur_line && max_line < cur_line) max_line = cur_line;
  }

  void run(const std::string& input) {
    rows.emplace_back();
    size_t i = 0;
    while (i < input.size()) {
      unsigned char ch = static_cast<unsigned char>(input[i]);
      switch (ch) {
        case '\b':  // col.c:400-407
          if (cur_col == 0) break;
          cur_col -= have_last ? last_width : 1;
          break;
        case '\r':  // col.c:408-410
          cur_col = 0;
          break;
        case '\033': {  // ESC: col.c:411-426
          if (i + 1 >= input.size()) break;
          unsigned char next = static_cast<unsigned char>(input[++i]);
          if (next == '7')       // RLF
            cur_line -= 2;
          else if (next == '8')  // RHLF
            cur_line -= 1;
          else if (next == '9')  // FHLF
            forward(1);
          // Any other escape is swallowed whole (col.c:423-424).
          break;
        }
        case '\n':  // col.c:428-433
          forward(2);
          cur_col = 0;
          break;
        case ' ':
          ++cur_col;
          break;
        case '\016':  // SO (col.c:440-442)
          cur_alt = true;
          break;
        case '\017':  // SI (col.c:437-439)
          cur_alt = false;
          break;
        case '\t':  // col.c:443-446
          cur_col |= 7;
          ++cur_col;
          break;
        case '\v':  // VT: reverse line feed (col.c:447-450)
          cur_line -= 2;
          break;
        case '\f':
          // iswspace with wcwidth -1: silently dropped (col.c:456-461).
          break;
        default:
          if (ch >= 0x21 && ch != 0x7F) {
            // Printable in the byte model; bytes >= 0x80 pass through
            // one byte at a time (the port's documented ASCII
            // approximation of wcwidth).
            store(static_cast<char>(ch), 1);
          } else if (cfg.pass_unknown) {
            // -p: unknown control sequences are stored and printed in
            // place, width -1 like wcwidth says (col.c:463-464).
            store(static_cast<char>(ch), -1);
          }
          break;
      }
      ++i;
    }

    // col.c:699-703: nothing was ever moved or written -> no output.
    if (max_line == 0 && cur_col == 0) return;
    flush_rows(node_line - origin + 1);
    if (last_alt) out.push_back('\017');  // leave the G0 set active
    // Trailing blank lines (col.c:709-715): recomputed from max_line.
    nblank = max_line - node_line;
    if (max_line & 1)
      ++nblank;
    else if (!nblank)
      nblank = 2;  // the last line had no newline: supply one
    flush_blanks();
  }
};

auto run(const Config& cfg) -> int {
  file_io::set_stdin_binary();
  std::string input{std::istreambuf_iterator<char>(std::cin),
                    std::istreambuf_iterator<char>()};
  Filter filter{cfg};
  filter.run(input);
  if (!filter.out.empty()) safePrint(filter.out);
  return 0;
}

}  // namespace col_pipeline

REGISTER_COMMAND(
    col, "col", "col [OPTION]...",
    "Filter reverse line feeds from standard input.\n"
    "\n"
    "col filters out reverse (and half-reverse) line feeds so that the\n"
    "output is in the correct order with only forward and half-forward\n"
    "line feeds.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options\n"
    "too.\n"
    "\n"
    "  -b, --no-backspaces    do not output backspaces\n"
    "  -f, --fine             permit forward half line feeds\n"
    "  -p, --pass             pass unknown control sequences\n"
    "  -h, --tabs             convert spaces to tabs (the default)\n"
    "  -x, --spaces           convert tabs to spaces\n"
    "  -l, --lines NUM        buffer at least NUM lines (default 32)",
    "  col -b < out.txt    strip backspaces from nroff output\n"
    "  col -x              output spaces instead of tabs",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COL_OPTIONS) {
  using namespace col_pipeline;

  Config cfg;
  int pre = prescan(ctx, cfg);
  if (pre >= 0) return pre;  // version printed (0) or an error printed (1)
  if (pre == -2) {           // -H/--help met in argv order
    print_usage();
    return 0;
  }
  cfg.no_backspaces = ctx.get<bool>("-b", false);
  cfg.fine = ctx.get<bool>("-f", false);
  cfg.pass_unknown = ctx.get<bool>("-p", false);
  // -h/-x exclusivity was checked by the prescan in argv order.
  if (ctx.get<bool>("-x", false)) cfg.compress_spaces = false;
  if (!ctx.positionals.empty()) {
    // col takes no operands (col.c:583-586).
    safeErrorPrintLn("col: bad usage");
    print_try_help();
    return 1;
  }
  return run(cfg);
}
