// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for column.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr COLUMN_OPTIONS = std::array{
    // [GNU] util-linux column.c longopts (column.c:796-826) and
    // optstring "C:c:dE:eH:hi:Jl:LN:n:mO:o:p:R:r:s:T:tVW:x"
    // (column.c:843).  The previously invented bindings (-x as
    // --output-fields, -e as --table-empty, -E as --table-noescape,
    // -N as --table-no-trunc, --output-width reading --columns) are
    // gone; every letter now means what column.c says it means.
    OPTION("-c", "--output-width", "width of output in number of characters",
           STRING_TYPE),
    OPTION("", "--columns", "width of output in number of characters",
           STRING_TYPE),  // deprecated alias of --output-width (column.c:798)
    OPTION("-x", "--fillrows", "fill rows before columns", BOOL_TYPE),
    OPTION("-t", "--table", "create a table", BOOL_TYPE),
    OPTION("-J", "--json", "use JSON output format for table", BOOL_TYPE),
    OPTION("-L", "--keep-empty-lines", "don't ignore empty lines", BOOL_TYPE),
    OPTION("", "--table-empty-lines", "don't ignore empty lines",
           BOOL_TYPE),  // deprecated alias (column.c:819)
    OPTION("-s", "--separator", "possible table delimiters", STRING_TYPE),
    OPTION("-o", "--output-separator",
           "columns separator for table output (default is two spaces)",
           STRING_TYPE),
    OPTION("-N", "--table-columns", "comma separated columns names",
           STRING_TYPE),
    OPTION("-C", "--table-column", "define column properties", STRING_TYPE),
    OPTION("-l", "--table-columns-limit", "maximal number of input columns",
           STRING_TYPE),
    OPTION("-n", "--table-name", "table name for JSON output", STRING_TYPE),
    OPTION("-O", "--table-order", "specify order of output columns",
           STRING_TYPE),
    OPTION("-E", "--table-noextreme",
           "don't count long text from the columns to column width",
           STRING_TYPE),
    OPTION("-d", "--table-noheadings", "don't print header", BOOL_TYPE),
    OPTION("-e", "--table-header-repeat", "repeat header for each page",
           BOOL_TYPE),
    OPTION("-m", "--table-maxout", "fill all available space", BOOL_TYPE),
    OPTION("-H", "--table-hide", "don't print the columns", STRING_TYPE),
    OPTION("-R", "--table-right", "right align text in these columns",
           STRING_TYPE),
    OPTION("-T", "--table-truncate",
           "truncate text in the columns when necessary", STRING_TYPE),
    OPTION("-W", "--table-wrap", "wrap text in the columns when necessary",
           STRING_TYPE),
    OPTION("-r", "--tree", "column to use tree-like output for the table",
           STRING_TYPE),
    OPTION("-i", "--tree-id", "line ID to specify child-parent relation",
           STRING_TYPE),
    OPTION("-p", "--tree-parent", "parent to specify child-parent relation",
           STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit",
           BOOL_TYPE),
    OPTION("-h", "--help", "display this help and exit", BOOL_TYPE)};

namespace column_pipeline {
namespace cp = core::pipeline;

enum class Mode { FillCols, FillRows, Table, Simple };

struct ColDef {
  std::string name;
  bool hidden = false;
  bool right = false;
  bool trunc = false;
  bool wrap = false;
  bool noextreme = false;
};

struct Config {
  Mode mode = Mode::FillCols;
  bool termwidth_given = false;
  size_t termwidth = 80;
  bool keep_empty_lines = false;
  bool json = false;
  bool noheadings = false;
  bool header_repeat = false;
  bool maxout = false;
  std::string input_separator;   // -s; empty = default "\t " greedy
  bool separator_given = false;
  std::string output_separator = "  ";
  std::string table_name;
  std::string table_order;
  std::vector<ColDef> coldefs;   // from -N / -C
  size_t maxncols = 0;           // -l
  std::string colhide, colright, coltrunc, colwrap, colnoextrem;
  bool any_table_option = false;  // a --table-* option that requires -t
  std::string tree, tree_id, tree_parent;
  std::vector<std::string> files;
};

// Display width, column.c:110-121 width(): sum of wcwidth over
// characters, counting only positive widths.  Byte model: printable
// ASCII counts 1, UTF-8 lead bytes count 1 (continuation bytes 0).
size_t text_width(std::string_view s) {
  size_t w = 0;
  for (unsigned char c : s) {
    if (c >= 0x20 && c != 0x7F)
      ++w;
    else if (c >= 0xC0)
      ++w;  // UTF-8 lead byte; continuation bytes add 0
  }
  return w;
}

bool is_space_byte(char c) {
  return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r';
}

// skip_space emptiness test (column.c:613-630): a line whose
// characters are all whitespace counts as empty.
bool line_is_empty(const std::string& line) {
  for (char c : line)
    if (!is_space_byte(c)) return false;
  return true;
}

// strtou32_or_err-shaped value (column.c:854,883): digits only.
bool parse_u32(std::string_view text, size_t& value) {
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return false;
  unsigned long long v = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<unsigned long long>(c - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  value = static_cast<size_t>(v);
  return true;
}

std::vector<std::string> split_commas(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, comma - start));
    start = comma + 1;
  }
  return out;
}

// getopt_long decides -V/-h, the -c/-l value errors and the
// exclusive-option groups in argv order (column.c:843-937); the
// set-based ctx cannot express that, so prescan ctx.raw_args.
// Returns the exit code to return at once (>= 0, output produced),
// -2 for "print usage, exit 0", -1 to continue.
int prescan(const CommandContext<COLUMN_OPTIONS.size()>& ctx) {
  const auto& args = ctx.raw_args;
  bool seen_C = false, seen_N = false, seen_J = false, seen_x = false,
       seen_t = false;
  auto exclusive = [&](const char* a, const char* b) {
    safeErrorPrintLn(std::string("column: mutually exclusive arguments: ") +
                     a + " " + b);
    return 1;
  };
  // Short options of column.c:843 that take a value.
  const std::string value_shorts = "CcEHilNnOopRrsTW";
  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view tok = args[i];
    if (tok == "--") break;
    if (tok == "--version") {
      safePrintLn("column (WinuxCmd) 1.0");
      return 0;
    }
    if (tok == "--help") return -2;
    if (tok == "--table-column") {
      if (seen_N) return exclusive("--table-column", "--table-columns");
      seen_C = true;
      ++i;
      continue;
    }
    if (tok == "--table-columns") {
      if (seen_C) return exclusive("--table-column", "--table-columns");
      seen_N = true;
      ++i;
      continue;
    }
    if (tok == "--json") {
      if (seen_x) return exclusive("--json", "--fillrows");
      seen_J = true;
      continue;
    }
    if (tok == "--fillrows") {
      if (seen_J) return exclusive("--json", "--fillrows");
      if (seen_t) return exclusive("--table", "--fillrows");
      seen_x = true;
      continue;
    }
    if (tok == "--table") {
      if (seen_x) return exclusive("--table", "--fillrows");
      seen_t = true;
      continue;
    }
    if (tok == "--output-width" || tok == "--columns" ||
        tok == "--table-columns-limit") {
      bool is_limit = tok == "--table-columns-limit";
      if (i + 1 >= args.size()) return -1;  // framework reports it
      std::string_view val = args[i + 1];
      size_t v = 0;
      if (!is_limit && val == "unlimited") {
        ++i;
        continue;
      }
      if (!parse_u32(val, v)) {
        safeErrorPrintLn(std::string("column: ") +
                         (is_limit ? "invalid columns limit argument: '"
                                   : "invalid columns argument: '") +
                         std::string(val) + "'");
        return 1;
      }
      if (is_limit && v == 0) {
        safeErrorPrintLn("column: columns limit must be greater than zero");
        return 1;
      }
      ++i;
      continue;
    }
    if (tok.starts_with("--output-width=") || tok.starts_with("--columns=") ||
        tok.starts_with("--table-columns-limit=")) {
      bool is_limit = tok.starts_with("--table-columns-limit=");
      std::string_view val = tok.substr(tok.find('=') + 1);
      size_t v = 0;
      if (!is_limit && val == "unlimited") continue;
      if (!parse_u32(val, v)) {
        safeErrorPrintLn(std::string("column: ") +
                         (is_limit ? "invalid columns limit argument: '"
                                   : "invalid columns argument: '") +
                         std::string(val) + "'");
        return 1;
      }
      if (is_limit && v == 0) {
        safeErrorPrintLn("column: columns limit must be greater than zero");
        return 1;
      }
      continue;
    }
    if (tok.starts_with("--")) {
      // Other long options: skip a following value for the ones that
      // take one, so it is not mistaken for an option token.
      static constexpr std::array<std::string_view, 12> long_values = {
          "--separator", "--output-separator", "--table-name",
          "--table-order", "--table-noextreme", "--table-hide",
          "--table-right", "--table-truncate", "--table-wrap",
          "--tree", "--tree-id", "--tree-parent"};
      for (auto lv : long_values)
        if (tok == lv) {
          ++i;
          break;
        }
      continue;
    }
    if (tok.size() >= 2 && tok[0] == '-') {
      for (size_t j = 1; j < tok.size(); ++j) {
        char c = tok[j];
        if (c == 'V') {
          safePrintLn("column (WinuxCmd) 1.0");
          return 0;
        }
        if (c == 'h') return -2;
        if (c == 'C') {
          if (seen_N) return exclusive("--table-column", "--table-columns");
          seen_C = true;
        } else if (c == 'N') {
          if (seen_C) return exclusive("--table-column", "--table-columns");
          seen_N = true;
        } else if (c == 'J') {
          if (seen_x) return exclusive("--json", "--fillrows");
          seen_J = true;
        } else if (c == 'x') {
          if (seen_J) return exclusive("--json", "--fillrows");
          if (seen_t) return exclusive("--table", "--fillrows");
          seen_x = true;
        } else if (c == 't') {
          if (seen_x) return exclusive("--table", "--fillrows");
          seen_t = true;
        }
        if (value_shorts.find(c) != std::string::npos) {
          // The rest of the token (or the next token) is the value.
          std::string_view val;
          bool have_val = true;
          if (j + 1 < tok.size())
            val = tok.substr(j + 1);
          else if (i + 1 < args.size())
            val = args[++i];
          else
            have_val = false;
          if (have_val && (c == 'c' || c == 'l')) {
            size_t v = 0;
            if (c == 'c' && val == "unlimited") break;
            if (!parse_u32(val, v)) {
              safeErrorPrintLn(
                  std::string("column: ") +
                  (c == 'l' ? "invalid columns limit argument: '"
                            : "invalid columns argument: '") +
                  std::string(val) + "'");
              return 1;
            }
            if (c == 'l' && v == 0) {
              safeErrorPrintLn(
                  "column: columns limit must be greater than zero");
              return 1;
            }
          }
          break;  // value consumed the rest of the token
        }
      }
      continue;
    }
    // Operand: getopt permutes, later options still count.
  }
  return -1;
}

void print_usage() {
  safePrintLn("Usage:");
  safePrintLn(" column [options] [<file>...]");
  safePrintLn("");
  safePrintLn("Columnate lists.");
  safePrintLn("");
  safePrintLn(" -t, --table                      create a table");
  safePrintLn(" -n, --table-name <name>          table name for JSON output");
  safePrintLn(" -O, --table-order <columns>      specify order of output columns");
  safePrintLn(" -C, --table-column <properties>  define column");
  safePrintLn(" -N, --table-columns <names>      comma separated columns names");
  safePrintLn(" -l, --table-columns-limit <num>  maximal number of input columns");
  safePrintLn(" -E, --table-noextreme <columns>  don't count long text from the columns to column width");
  safePrintLn(" -d, --table-noheadings           don't print header");
  safePrintLn(" -m, --table-maxout               fill all available space");
  safePrintLn(" -e, --table-header-repeat        repeat header for each page");
  safePrintLn(" -H, --table-hide <columns>       don't print the columns");
  safePrintLn(" -R, --table-right <columns>      right align text in these columns");
  safePrintLn(" -T, --table-truncate <columns>   truncate text in the columns when necessary");
  safePrintLn(" -W, --table-wrap <columns>       wrap text in the columns when necessary");
  safePrintLn(" -L, --keep-empty-lines           don't ignore empty lines");
  safePrintLn(" -J, --json                       use JSON output format for table");
  safePrintLn("");
  safePrintLn(" -r, --tree <column>              column to use tree-like output for the table");
  safePrintLn(" -i, --tree-id <column>           line ID to specify child-parent relation");
  safePrintLn(" -p, --tree-parent <column>       parent to specify child-parent relation");
  safePrintLn("");
  safePrintLn(" -c, --output-width <width>       width of output in number of characters");
  safePrintLn(" -o, --output-separator <string>  columns separator for table output (default is two spaces)");
  safePrintLn(" -s, --separator <string>         possible table delimiters");
  safePrintLn(" -x, --fillrows                   fill rows before columns");
  safePrintLn("");
  safePrintLn(" -h, --help                       display this help");
  safePrintLn(" -V, --version                    display version");
}

std::string get_str(const CommandContext<COLUMN_OPTIONS.size()>& ctx,
                    const char* key, const char* fallback_key = nullptr) {
  std::string v = ctx.get<std::string>(key, "");
  if (v.empty() && fallback_key) v = ctx.get<std::string>(fallback_key, "");
  return v;
}

// Parse one -C property list ("name=X,right=1,...") into a ColDef.
ColDef parse_coldef(const std::string& props) {
  ColDef def;
  for (const auto& part : split_commas(props)) {
    auto eq = part.find('=');
    if (eq == std::string::npos) continue;
    std::string_view key = std::string_view(part).substr(0, eq);
    std::string_view val = std::string_view(part).substr(eq + 1);
    bool on = val == "1" || val == "true";
    if (key == "name")
      def.name = std::string(val);
    else if (key == "right")
      def.right = on;
    else if (key == "trunc")
      def.trunc = on;
    else if (key == "wrap")
      def.wrap = on;
    else if (key == "noextreme" || key == "noextremes")
      def.noextreme = on;
    else if (key == "hidden")
      def.hidden = on;
  }
  return def;
}

auto build_config(const CommandContext<COLUMN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  std::string width = get_str(ctx, "--output-width", "--columns");
  if (!width.empty()) {
    cfg.termwidth_given = true;
    if (width == "unlimited")
      cfg.termwidth = 0;
    else {
      size_t v = 0;
      parse_u32(width, v);  // already validated by the prescan
      cfg.termwidth = v;
    }
  }
  cfg.keep_empty_lines = ctx.get<bool>("--keep-empty-lines", false) ||
                         ctx.get<bool>("--table-empty-lines", false) ||
                         ctx.get<bool>("-L", false);
  cfg.json = ctx.get<bool>("--json", false) || ctx.get<bool>("-J", false);
  cfg.noheadings = ctx.get<bool>("--table-noheadings", false) ||
                   ctx.get<bool>("-d", false);
  cfg.header_repeat = ctx.get<bool>("--table-header-repeat", false) ||
                      ctx.get<bool>("-e", false);
  cfg.maxout = ctx.get<bool>("--table-maxout", false) ||
               ctx.get<bool>("-m", false);
  cfg.input_separator = get_str(ctx, "--separator", "-s");
  cfg.separator_given = !cfg.input_separator.empty();
  std::string osep = get_str(ctx, "--output-separator", "-o");
  if (!osep.empty()) cfg.output_separator = osep;
  cfg.table_name = get_str(ctx, "--table-name", "-n");
  cfg.table_order = get_str(ctx, "--table-order", "-O");
  cfg.colhide = get_str(ctx, "--table-hide", "-H");
  cfg.colright = get_str(ctx, "--table-right", "-R");
  cfg.coltrunc = get_str(ctx, "--table-truncate", "-T");
  cfg.colwrap = get_str(ctx, "--table-wrap", "-W");
  cfg.colnoextrem = get_str(ctx, "--table-noextreme", "-E");
  cfg.tree = get_str(ctx, "--tree", "-r");
  cfg.tree_id = get_str(ctx, "--tree-id", "-i");
  cfg.tree_parent = get_str(ctx, "--tree-parent", "-p");

  std::string names = get_str(ctx, "--table-columns", "-N");
  if (!names.empty())
    for (const auto& n : split_commas(names)) {
      ColDef def;
      def.name = n;
      cfg.coldefs.push_back(def);
    }
  std::string coldef = get_str(ctx, "--table-column", "-C");
  if (!coldef.empty()) cfg.coldefs.push_back(parse_coldef(coldef));

  std::string limit = get_str(ctx, "--table-columns-limit", "-l");
  if (!limit.empty()) {
    size_t v = 0;
    parse_u32(limit, v);  // validated by the prescan
    cfg.maxncols = v;
  }

  bool table = ctx.get<bool>("--table", false) || ctx.get<bool>("-t", false);
  bool fillrows =
      ctx.get<bool>("--fillrows", false) || ctx.get<bool>("-x", false);
  if (table || cfg.json || !cfg.tree.empty())
    cfg.mode = Mode::Table;
  else if (fillrows)
    cfg.mode = Mode::FillRows;

  // column.c:955-960: these options are meaningless without a table.
  cfg.any_table_option =
      !cfg.table_order.empty() || !cfg.table_name.empty() ||
      !cfg.colwrap.empty() || !cfg.colhide.empty() || !cfg.coltrunc.empty() ||
      !cfg.colnoextrem.empty() || !cfg.colright.empty() || !names.empty() ||
      !coldef.empty();

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files)
          cfg.files.push_back(wstring_to_utf8(file));
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }
  return cfg;
}

// Read one input (column.c read_input + main loop, column.c:603-665,
// 968-979): a missing file warns and is skipped (the caller sums the
// failure into the exit code); a directory is the oracle's fatal
// "read failed" path.
auto read_one(const std::string& filename, std::string& content, int& eval)
    -> bool {
  if (filename.empty()) return true;
  std::error_code ec;
  auto path = native_path::normalize_api_operand(filename);
  if (std::filesystem::is_directory(std::filesystem::u8path(path), ec) && !ec) {
    safeErrorPrintLn("column: read failed: Is a directory");
    eval = -1;  // fatal
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    safeErrorPrintLn("column: " + filename +
                     ": No such file or directory");
    eval += 1;
    return true;  // warn and continue with the next file
  }
  content.append(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
  return true;
}

// Split content into lines on '\n' (getline semantics: a trailing
// newline does not produce a final empty line).
std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(content.substr(start));
      break;
    }
    lines.push_back(content.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

void print_fill(const Config& cfg, const std::vector<std::string>& ents,
                size_t maxlength) {
  std::string out;
  size_t nents = ents.size();
  if (cfg.mode == Mode::Simple) {
    for (const auto& e : ents) {
      out += e;
      out += '\n';
    }
    safePrint(out);
    return;
  }
  // column.c:671,700: the column pitch is the longest entry rounded
  // up to a tab stop, always advancing at least one stop.
  size_t pitch = (maxlength + 8) & ~size_t{7};
  size_t numcols = pitch ? cfg.termwidth / pitch : 0;
  if (cfg.mode == Mode::FillCols) {
    if (!numcols) numcols = 1;
    size_t numrows = nents / numcols + (nents % numcols ? 1 : 0);
    for (size_t row = 0; row < numrows; ++row) {
      size_t endcol = pitch;
      size_t chcnt = 0;
      for (size_t col = 0, base = row; col < numcols; ++col) {
        out += ents[base];
        chcnt += text_width(ents[base]);
        if ((base += numrows) >= nents) break;
        size_t cnt;
        while ((cnt = (chcnt + 8) & ~size_t{7}) <= endcol) {
          out += '\t';
          chcnt = cnt;
        }
        endcol += pitch;
      }
      out += '\n';
    }
  } else {  // FillRows
    if (!numcols) numcols = 1;
    size_t endcol = pitch;
    size_t chcnt = 0, col = 0;
    for (size_t idx = 0; idx < nents; ++idx) {
      out += ents[idx];
      chcnt += text_width(ents[idx]);
      if (idx + 1 == nents) break;
      if (++col == numcols) {
        chcnt = col = 0;
        endcol = pitch;
        out += '\n';
      } else {
        size_t cnt;
        while ((cnt = (chcnt + 8) & ~size_t{7}) <= endcol) {
          out += '\t';
          chcnt = cnt;
        }
        endcol += pitch;
      }
    }
    if (chcnt) out += '\n';
  }
  safePrint(out);
}

// ---- table mode -------------------------------------------------

// Split one line into cells (column.c local_wcstok, column.c:166-190,
// called from add_line_to_table, column.c:512-570).  Default (no -s):
// greedy on "\t " — delimiter runs collapse, no empty cells.  With
// -s: every delimiter character splits and empty cells are kept,
// including a final empty cell after a trailing delimiter.  With -l,
// the cell at the limit keeps the rest of the line unsplit.
std::vector<std::string> split_cells(const std::string& line,
                                     const Config& cfg) {
  std::vector<std::string> cells;
  if (!cfg.separator_given) {
    size_t pos = 0;
    while (pos < line.size()) {
      pos = line.find_first_not_of(" \t", pos);
      if (pos == std::string::npos) break;
      if (cfg.maxncols && cells.size() + 1 == cfg.maxncols) {
        cells.push_back(line.substr(pos));
        return cells;
      }
      size_t end = line.find_first_of(" \t", pos);
      if (end == std::string::npos) {
        cells.push_back(line.substr(pos));
        break;
      }
      cells.push_back(line.substr(pos, end - pos));
      pos = end;
    }
    return cells;
  }
  const std::string& sep = cfg.input_separator;
  size_t start = 0;
  while (true) {
    if (cfg.maxncols && cells.size() + 1 == cfg.maxncols) {
      cells.push_back(line.substr(start));
      return cells;
    }
    size_t end = line.find_first_of(sep, start);
    if (end == std::string::npos) {
      cells.push_back(line.substr(start));
      break;
    }
    cells.push_back(line.substr(start, end - start));
    start = end + 1;
    if (start > line.size()) break;
    if (start == line.size()) {
      // A trailing delimiter leaves one final empty cell.
      if (!cfg.maxncols || cells.size() + 1 < cfg.maxncols)
        cells.push_back("");
      else if (cfg.maxncols && cells.size() + 1 == cfg.maxncols)
        cells.push_back("");
      break;
    }
  }
  return cells;
}

// Resolve a --table-* column list (names, 1-based numbers, or
// number ranges) to column indexes (modify_table, column.c:263-470).
std::vector<size_t> resolve_columns(const std::string& spec,
                                    const std::vector<ColDef>& cols) {
  std::vector<size_t> out;
  for (const auto& token : split_commas(spec)) {
    if (token.empty()) continue;
    auto dash = token.find('-', 1);
    size_t lo = 0, hi = 0;
    if (dash != std::string::npos &&
        parse_u32(token.substr(0, dash), lo) &&
        parse_u32(token.substr(dash + 1), hi) && lo >= 1 && hi >= lo) {
      for (size_t n = lo; n <= hi && n <= cols.size(); ++n)
        out.push_back(n - 1);
      continue;
    }
    size_t num = 0;
    if (parse_u32(token, num) && num >= 1 && num <= cols.size()) {
      out.push_back(num - 1);
      continue;
    }
    for (size_t i = 0; i < cols.size(); ++i)
      if (cols[i].name == token) {
        out.push_back(i);
        break;
      }
  }
  return out;
}

std::string json_escape(const std::string& s) {
  // lib/jsonwrt.c fputs_quoted_case_json: quotes and backslashes are
  // escaped, the named control escapes are used, other control
  // characters become \u00XX; bytes >= 0x80 pass through.
  std::string out;
  char buf[8];
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\f': out += "\\f"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 0x20) {
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string ascii_lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

int print_table(const Config& cfg, std::vector<ColDef> cols,
                const std::vector<std::vector<std::string>>& rows) {
  // Apply the modify_table lists (column.c:250-505).
  for (size_t i : resolve_columns(cfg.colhide, cols)) cols[i].hidden = true;
  for (size_t i : resolve_columns(cfg.colright, cols)) cols[i].right = true;
  for (size_t i : resolve_columns(cfg.coltrunc, cols)) cols[i].trunc = true;
  for (size_t i : resolve_columns(cfg.colwrap, cols)) cols[i].wrap = true;
  for (size_t i : resolve_columns(cfg.colnoextrem, cols))
    cols[i].noextreme = true;
  // --table-order is applied last (column.c:504-505): listed columns
  // move to the front in the listed order.
  std::vector<size_t> order;
  if (!cfg.table_order.empty()) {
    auto wanted = resolve_columns(cfg.table_order, cols);
    std::vector<bool> used(cols.size(), false);
    for (size_t i : wanted)
      if (!used[i]) {
        order.push_back(i);
        used[i] = true;
      }
    for (size_t i = 0; i < cols.size(); ++i)
      if (!used[i]) order.push_back(i);
  } else {
    for (size_t i = 0; i < cols.size(); ++i) order.push_back(i);
  }
  std::vector<size_t> visible;
  for (size_t i : order)
    if (!cols[i].hidden) visible.push_back(i);

  if (cfg.json) {
    // lib/jsonwrt.c: 3-space indent unit, member names lowercased,
    // empty or missing data becomes null, row objects join with "},{"
    std::string name =
        ascii_lower(cfg.table_name.empty() ? "table" : cfg.table_name);
    std::string out = "{\n   \"" + json_escape(name) + "\": [\n";
    bool first_row = true;
    for (const auto& row : rows) {
      // jsonwrt: an unnamed object after a close is ",{" with no
      // indent (lib/jsonwrt.c ul_jsonwrt_open).
      if (!first_row)
        out += ",{\n";
      else
        out += "      {\n";
      first_row = false;
      bool first_cell = true;
      for (size_t vi : visible) {
        if (!first_cell) out += ",\n";
        first_cell = false;
        out += "         \"" + json_escape(ascii_lower(cols[vi].name)) +
               "\": ";
        const std::string* cell =
            vi < row.size() && !row[vi].empty() ? &row[vi] : nullptr;
        if (cell)
          out += "\"" + json_escape(*cell) + "\"";
        else
          out += "null";
      }
      out += "\n      }";
    }
    out += "\n   ]\n}\n";
    safePrint(out);
    return 0;
  }

  // Column widths: header name and every cell (libsmartcols base
  // widths; the noextreme/trunc flags only matter when shrinking).
  std::vector<size_t> widths(cols.size(), 0);
  for (size_t i = 0; i < cols.size(); ++i) {
    widths[i] = text_width(cols[i].name);
    for (const auto& row : rows)
      if (i < row.size())
        widths[i] = std::max(widths[i], text_width(row[i]));
  }
  auto total_width = [&]() {
    size_t total = 0;
    for (size_t k = 0; k < visible.size(); ++k) {
      if (k) total += cfg.output_separator.size();
      total += widths[visible[k]];
    }
    return total;
  };
  // Shrink pass (libsmartcols calculate.c, simplified): only trunc
  // or noextreme columns give up width, widest first, floor 1.
  if (cfg.termwidth > 0) {
    while (total_width() > cfg.termwidth) {
      size_t best = cols.size();
      for (size_t vi : visible)
        if ((cols[vi].trunc || cols[vi].noextreme) && widths[vi] > 1 &&
            (best == cols.size() || widths[vi] > widths[best]))
          best = vi;
      if (best == cols.size()) break;
      --widths[best];
    }
  }

  auto render_row = [&](const std::vector<std::string>& cells,
                          bool is_header) -> std::vector<std::string> {
    // Returns one or more physical lines (wrapping -W columns).
    std::vector<std::vector<std::string>> segments(visible.size());
    size_t lines_needed = 1;
    for (size_t k = 0; k < visible.size(); ++k) {
      size_t vi = visible[k];
      std::string cell =
          vi < cells.size() ? cells[vi] : std::string{};
      size_t w = widths[vi];
      if (!is_header && (cols[vi].trunc || cols[vi].noextreme) &&
          text_width(cell) > w) {
        // Cut by display width (byte-walk keeping whole UTF-8 chars
        // approximately: cut at the byte where the width runs out).
        std::string cut;
        size_t cw = 0;
        for (size_t b = 0; b < cell.size();) {
          size_t step = 1;
          unsigned char lead = static_cast<unsigned char>(cell[b]);
          if (lead >= 0xF0) step = 4;
          else if (lead >= 0xE0) step = 3;
          else if (lead >= 0xC0) step = 2;
          if (b + step > cell.size()) step = 1;
          size_t add = (lead >= 0x20 && lead != 0x7F) || lead >= 0xC0 ? 1 : 0;
          if (cw + add > w) break;
          cw += add;
          cut.append(cell, b, step);
          b += step;
        }
        cell = cut;
      }
      if (!is_header && cols[vi].wrap && w > 0 && text_width(cell) > w) {
        for (size_t b = 0; b < cell.size(); b += w)
          segments[k].push_back(cell.substr(b, w));
      } else {
        segments[k].push_back(cell);
      }
      lines_needed = std::max(lines_needed, segments[k].size());
    }
    std::vector<std::string> lines;
    for (size_t li = 0; li < lines_needed; ++li) {
      std::string line;
      for (size_t k = 0; k < visible.size(); ++k) {
        size_t vi = visible[k];
        std::string cell =
            li < segments[k].size() ? segments[k][li] : std::string{};
        size_t w = widths[vi];
        size_t cw = text_width(cell);
        bool last = k + 1 == visible.size();
        if (cols[vi].right) {
          if (cw < w) line.append(w - cw, ' ');
          line += cell;
        } else {
          line += cell;
          if (!last && cw < w) line.append(w - cw, ' ');
        }
        if (!last) line += cfg.output_separator;
      }
      lines.push_back(line);
    }
    return lines;
  };

  std::string out;
  bool any_named = false;
  for (size_t vi : visible)
    if (!cols[vi].name.empty()) any_named = true;
  if (!cfg.noheadings && any_named) {
    std::vector<std::string> header(cols.size());
    for (size_t i = 0; i < cols.size(); ++i) header[i] = cols[i].name;
    for (const auto& l : render_row(header, true)) {
      out += l;
      out += '\n';
    }
  }
  for (const auto& row : rows) {
    if (row.empty()) {
      out += '\n';
      continue;
    }
    for (const auto& l : render_row(row, false)) {
      out += l;
      out += '\n';
    }
  }
  safePrint(out);
  return 0;
}

auto run(const Config& cfg, int& eval) -> int {
  // Lines are gathered per source: the oracle runs getline per FILE
  // (column.c:968-979), so an unterminated last line of one file
  // never merges with the first line of the next.
  std::vector<std::string> lines;
  if (cfg.files.empty()) {
    file_io::set_stdin_binary();
    std::string content{std::istreambuf_iterator<char>(std::cin),
                        std::istreambuf_iterator<char>()};
    lines = split_lines(content);
  } else {
    for (const auto& file : cfg.files) {
      std::string content;
      if (!read_one(file, content, eval)) return 1;  // fatal (directory)
      auto part = split_lines(content);
      lines.insert(lines.end(), part.begin(), part.end());
    }
  }

  if (cfg.mode == Mode::Table) {
    std::vector<ColDef> cols = cfg.coldefs;
    // -H with a "-" entry marks unnamed columns hidden, which is
    // also what lets JSON grow unnamed columns (column.c:867-870,
    // 543-550).
    bool hide_unnamed = false;
    for (const auto& t : split_commas(cfg.colhide))
      if (t == "-") hide_unnamed = true;
    std::vector<std::vector<std::string>> rows;
    for (const auto& line : lines) {
      if (line_is_empty(line)) {
        if (cfg.keep_empty_lines) rows.emplace_back();
        continue;
      }
      std::vector<std::string> cells = split_cells(line, cfg);
      for (size_t n = cols.size(); n < cells.size(); ++n) {
        if (cfg.json && !hide_unnamed) {
          safeErrorPrintLn("column: line " +
                           std::to_string(rows.size() + 1) +
                           ": for JSON the name of the column " +
                           std::to_string(n + 1) + " is required");
          return 1;
        }
        ColDef def;
        def.hidden = hide_unnamed;
        cols.push_back(def);
      }
      rows.push_back(std::move(cells));
    }
    // column.c:984: no lines were ever added -> no output at all.
    if (rows.empty()) return eval ? 1 : 0;
    return print_table(cfg, std::move(cols), rows);
  }

  std::vector<std::string> ents;
  size_t maxlength = 0;
  for (const auto& line : lines) {
    if (line_is_empty(line)) {
      if (cfg.keep_empty_lines) ents.emplace_back("");
      continue;
    }
    maxlength = std::max(maxlength, text_width(line));
    ents.push_back(line);
  }
  if (ents.empty()) return eval ? 1 : 0;
  Config eff = cfg;
  if (eff.mode != Mode::Table && maxlength >= eff.termwidth)
    eff.mode = Mode::Simple;  // column.c:973-977
  print_fill(eff, ents, maxlength);
  return eval ? 1 : 0;
}

}  // namespace column_pipeline

REGISTER_COMMAND(column, "column", "column [options] [<file>...]",
                 "Columnate lists.\n"
                 "\n"
                 " -t, --table                      create a table\n"
                 " -x, --fillrows                   fill rows before columns\n"
                 " -J, --json                       use JSON output format for table\n"
                 " -N, --table-columns <names>      comma separated columns names\n"
                 " -c, --output-width <width>       width of output in number of characters\n"
                 " -s, --separator <string>         possible table delimiters\n"
                 " -o, --output-separator <string>  columns separator for table output (default is two spaces)",
                 "  column < list.txt\n"
                 "  column -x list.txt\n"
                 "  column -t -s : /etc/passwd",
                 "col(1), colrm(1), ls(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", COLUMN_OPTIONS) {
  using namespace column_pipeline;

  int pre = prescan(ctx);
  if (pre >= 0) return pre;
  if (pre == -2) {
    print_usage();
    return 0;
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"column");
    return 1;
  }
  Config cfg = *cfg_result;
  if (!cfg.termwidth_given) {
    // get_terminal_width(80) (column.c:944-945): console width when
    // attached, else the COLUMNS environment value, else 80.
    if (isOutputConsole()) {
      cfg.termwidth = static_cast<size_t>(getConsoleViewportSize().first);
    } else if (const char* env = std::getenv("COLUMNS")) {
      size_t v = 0;
      if (parse_u32(env, v) && v > 0) cfg.termwidth = v;
    }
  }
  // Post-parse requirement errors (column.c:947-965), in oracle order.
  if (!cfg.tree.empty() && (cfg.tree_id.empty() || cfg.tree_parent.empty())) {
    safeErrorPrintLn(
        "column: options --tree-id and --tree-parent are required for "
        "tree formatting");
    return 1;
  }
  if (cfg.mode != Mode::Table && cfg.any_table_option) {
    safeErrorPrintLn("column: option --table required for all --table-*");
    return 1;
  }
  if (cfg.coldefs.empty() && cfg.json) {
    safeErrorPrintLn(
        "column: option --table-columns or --table-column required for "
        "--json");
    return 1;
  }

  int eval = 0;
  int rc = run(cfg, eval);
  if (rc != 0) return rc;
  return eval ? 1 : 0;
}
