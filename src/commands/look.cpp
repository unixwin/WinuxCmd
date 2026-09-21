// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for look (display lines beginning with a
/// string).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// [GNU] -d, --alphanum: use only alphanumeric and blanks for comparison
// [GNU] -f, --ignore-case: ignore case when comparing
// [GNU] -t, --terminate: specify the string-terminator character
// [GNU] -a, --alternative: use alternative dictionary order (whole first token)
auto constexpr LOOK_OPTIONS = std::array{
    OPTION("-d", "--alphanum", "compare only alphanumeric and blanks"),
    OPTION("-f", "--ignore-case", "ignore case when comparing"),
    OPTION("-a", "--alternative", "use alternative dictionary order"),
    OPTION("-t", "--terminate", "terminate string at CHAR", STRING_TYPE),
    OPTION("", "--version", "output version information and exit")};

namespace look_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool alphanum = false;
  bool ignore_case = false;
  bool alternative = false;
  std::string terminator;  // empty means none
  std::string key;
  std::string file;
};

auto build_config(const CommandContext<LOOK_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.alphanum =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--alphanum", false);
  cfg.ignore_case =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--ignore-case", false);
  cfg.alternative =
      ctx.get<bool>("-a", false) || ctx.get<bool>("--alternative", false);

  if (ctx.has("-t") || ctx.has("--terminate")) {
    auto t =
        ctx.get<std::string>("--terminate", ctx.get<std::string>("-t", ""));
    if (!t.empty()) cfg.terminator = t;
  }

  // Positional args: string [file]
  SmallVector<std::string, 4> pos;
  for (auto a : ctx.positionals) pos.push_back(std::string(a));
  if (pos.empty()) {
    return std::unexpected("missing operand");
  }
  cfg.key = pos[0];
  if (pos.size() >= 2) {
    cfg.file = pos[1];
  }
  return cfg;
}

// Normalize a character per -d/-f flags for comparison purposes.
auto norm_char(char c, bool alphanum, bool ignore_case) -> char {
  if (ignore_case) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (alphanum) {
    // keep alnum and blanks; everything else becomes blank (space) for sorting
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '\t') {
      return ' ';
    }
  }
  return c;
}

// Apply -t terminator: only compare up to and including the terminator char.
// Returns the effective comparison prefix length for the key, and the per-line
// effective comparison length (line up to terminator inclusive).
auto key_for_compare(std::string_view s, const std::string& terminator,
                     bool alphanum, bool ignore_case) -> std::string {
  std::string out;
  for (char c : s) {
    out.push_back(norm_char(c, alphanum, ignore_case));
  }
  if (!terminator.empty()) {
    auto pos = out.find_first_of(terminator);
    if (pos != std::string::npos) {
      out.resize(pos + 1);
    }
  }
  return out;
}

auto line_for_compare(std::string_view line, const std::string& terminator,
                      bool alphanum, bool ignore_case) -> std::string {
  std::string out;
  for (char c : line) {
    out.push_back(norm_char(c, alphanum, ignore_case));
  }
  if (!terminator.empty()) {
    auto pos = out.find_first_of(terminator);
    if (pos != std::string::npos) {
      out.resize(pos + 1);
    }
  }
  return out;
}

// Read all lines from a file.
auto read_lines(const std::string& filename, std::vector<std::string>& out)
    -> bool {
  std::wstring wfn = utf8_to_wstring(filename);
  HANDLE h = CreateFileW(wfn.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
    CloseHandle(h);
    return false;
  }
  std::string data(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD got = 0;
  BOOL ok = TRUE;
  if (!data.empty()) {
    ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &got,
                  nullptr);
  }
  CloseHandle(h);
  if (!ok) return false;
  data.resize(got);

  size_t start = 0;
  while (start <= data.size()) {
    size_t nl = data.find('\n', start);
    if (nl == std::string::npos) {
      out.emplace_back(data.substr(start));
      break;
    }
    std::string line = data.substr(start, nl - start);
    // strip a trailing \r (CRLF files)
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    start = nl + 1;
  }
  return true;
}

auto run(const Config& cfg) -> int {
  if (cfg.file.empty()) {
    // GNU falls back to /usr/share/dict/words; on Windows there is no
    // canonical dictionary, so require an explicit file.
    safeErrorPrintLn("look: no dictionary file given");
    safeErrorPrintLn("Try 'look --help' for more information.");
    return 1;
  }

  std::vector<std::string> lines;
  if (!read_lines(cfg.file, lines)) {
    safeErrorPrintLn("look: cannot open '" + cfg.file + "'");
    return 1;
  }

  if (lines.empty()) return 0;

  // Binary search for the lower bound of the key prefix.
  std::string key =
      key_for_compare(cfg.key, cfg.terminator, cfg.alphanum, cfg.ignore_case);

  // lower_bound: first line whose compare-form >= key.
  size_t lo = 0;
  size_t hi = lines.size();
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    std::string cmp = line_for_compare(lines[mid], cfg.terminator, cfg.alphanum,
                                       cfg.ignore_case);
    if (cmp < key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  // Emit every consecutive line whose compare-form starts with key.
  int printed = 0;
  for (size_t i = lo; i < lines.size(); ++i) {
    std::string cmp = line_for_compare(lines[i], cfg.terminator, cfg.alphanum,
                                       cfg.ignore_case);
    if (cmp.compare(0, key.size(), key) != 0) {
      // not a prefix match
      if (!cmp.empty() && key.empty()) {
        // keep going only if key empty
      } else {
        break;
      }
    }
    if (cmp.size() >= key.size() && cmp.compare(0, key.size(), key) == 0) {
      safePrintLn(lines[i]);
      ++printed;
    }
  }
  return 0;
}

}  // namespace look_pipeline

REGISTER_COMMAND(
    look, "look", "look [OPTION]... STRING [FILE]",
    "Display lines beginning with STRING.  Performs a binary search, so "
    "FILE must be sorted.  With no FILE, a default dictionary is used "
    "(not available on Windows, so an explicit FILE is required).",
    "  look apple words.txt\n"
    "  look -f apple words.txt",
    "grep(1), sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LOOK_OPTIONS) {
  using namespace look_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("look: missing operand");
      safeErrorPrintLn("Try 'look --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"look");
    return 1;
  }

  return run(*cfg_result);
}
