/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: diff3.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for diff3 command.
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

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr DIFF3_OPTIONS =
    std::array{OPTION("-e", "", "output ed script"),
               OPTION("-E", "", "output ed script with bracketed conflicts"),
               OPTION("-m", "", "output in merged format"),
               OPTION("-A", "", "like -E, but overwrite overlapping changes"),
               OPTION("-a", "", "treat all files as text")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Diff two lists of lines
struct DiffResult {
  std::vector<std::pair<int, int>> changes;  // Start, count
};

DiffResult diff_lines(const std::vector<std::string>& old_lines,
                      const std::vector<std::string>& new_lines) {
  DiffResult result;

  // Simple diff algorithm
  size_t old_idx = 0;
  size_t new_idx = 0;

  while (old_idx < old_lines.size() && new_idx < new_lines.size()) {
    if (old_lines[old_idx] == new_lines[new_idx]) {
      old_idx++;
      new_idx++;
    } else {
      // Find matching line
      bool found = false;
      for (size_t j = new_idx; j < new_lines.size(); ++j) {
        if (old_lines[old_idx] == new_lines[j]) {
          result.changes.push_back(
              {static_cast<int>(old_idx), static_cast<int>(j - new_idx)});
          new_idx = j;
          found = true;
          break;
        }
      }

      if (!found) {
        result.changes.push_back({static_cast<int>(old_idx), 1});
        old_idx++;
      }
    }
  }

  // Handle remaining lines
  if (old_idx < old_lines.size()) {
    result.changes.push_back({static_cast<int>(old_idx),
                              static_cast<int>(old_lines.size() - old_idx)});
  }

  return result;
}

// Merge three files
std::vector<std::string> merge_files(const std::vector<std::string>& mine,
                                     const std::vector<std::string>& older,
                                     const std::vector<std::string>& yours) {
  std::vector<std::string> result;

  // Simple merge: prefer mine, mark conflicts
  DiffResult mine_older = diff_lines(older, mine);
  DiffResult older_yours = diff_lines(older, yours);

  size_t old_idx = 0;
  size_t mine_idx = 0;
  size_t yours_idx = 0;

  while (old_idx < older.size()) {
    // Check if this line was changed in mine
    bool mine_changed = false;
    for (const auto& change : mine_older.changes) {
      if (change.first == static_cast<int>(old_idx)) {
        mine_changed = true;
        break;
      }
    }

    // Check if this line was changed in yours
    bool yours_changed = false;
    for (const auto& change : older_yours.changes) {
      if (change.first == static_cast<int>(old_idx)) {
        yours_changed = true;
        break;
      }
    }

    if (mine_changed && yours_changed) {
      // Conflict
      result.push_back("<<<<<<< MINE");
      if (mine_idx < mine.size()) {
        result.push_back(mine[mine_idx++]);
      }
      result.push_back("=======");
      if (yours_idx < yours.size()) {
        result.push_back(yours[yours_idx++]);
      }
      result.push_back(">>>>>>> YOURS");
      old_idx++;
    } else if (mine_changed) {
      if (mine_idx < mine.size()) {
        result.push_back(mine[mine_idx++]);
      }
      old_idx++;
    } else if (yours_changed) {
      if (yours_idx < yours.size()) {
        result.push_back(yours[yours_idx++]);
      }
      old_idx++;
    } else {
      if (mine_idx < mine.size()) {
        result.push_back(mine[mine_idx++]);
      }
      if (yours_idx < yours.size()) {
        yours_idx++;
      }
      old_idx++;
    }
  }

  // Add remaining lines
  while (mine_idx < mine.size()) {
    result.push_back(mine[mine_idx++]);
  }

  return result;
}
}  // namespace

// ======================================================
// Pipeline components
// ======================================================
namespace diff3_pipeline {
namespace cp = core::pipeline;

auto resolve_files(const CommandContext<DIFF3_OPTIONS.size()>& ctx)
    -> cp::Result<std::tuple<std::string, std::string, std::string>> {
  std::vector<std::string> files;

  for (const auto& positional : ctx.positionals) {
    std::string file_arg = std::string(positional);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    files.push_back(file_arg);
  }

  if (files.size() < 3) {
    return std::unexpected("missing file operands");
  }
  if (files.size() > 3) {
    return std::unexpected("too many file operands");
  }

  return std::make_tuple(files[0], files[1], files[2]);
}

struct Config {
  bool merged_output = false;
  bool ed_script = false;
  bool bracketed_conflicts = false;
  bool overwrite_overlapping = false;
  bool treat_as_text = false;
  std::string mine_file;
  std::string older_file;
  std::string yours_file;
};

auto build_config(const CommandContext<DIFF3_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.merged_output = ctx.get<bool>("-m", false);
  cfg.ed_script = ctx.get<bool>("-e", false);
  cfg.bracketed_conflicts = ctx.get<bool>("-E", false);
  cfg.overwrite_overlapping = ctx.get<bool>("-A", false);
  cfg.treat_as_text = ctx.get<bool>("-a", false);

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    return std::unexpected(files_result.error());
  }

  cfg.mine_file = std::get<0>(*files_result);
  cfg.older_file = std::get<1>(*files_result);
  cfg.yours_file = std::get<2>(*files_result);

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Read files
  std::vector<std::string> mine_lines = read_file_lines(cfg.mine_file);
  std::vector<std::string> older_lines = read_file_lines(cfg.older_file);
  std::vector<std::string> yours_lines = read_file_lines(cfg.yours_file);

  if (mine_lines.empty()) {
    safeErrorPrint("diff3: cannot read file '");
    safeErrorPrint(cfg.mine_file);
    safeErrorPrintLn("'");
    return 1;
  }
  if (older_lines.empty()) {
    safeErrorPrint("diff3: cannot read file '");
    safeErrorPrint(cfg.older_file);
    safeErrorPrintLn("'");
    return 1;
  }
  if (yours_lines.empty()) {
    safeErrorPrint("diff3: cannot read file '");
    safeErrorPrint(cfg.yours_file);
    safeErrorPrintLn("'");
    return 1;
  }

  // Compare files
  DiffResult mine_older = diff_lines(older_lines, mine_lines);
  DiffResult older_yours = diff_lines(older_lines, yours_lines);

  if (cfg.merged_output) {
    // Output merged format
    std::vector<std::string> merged =
        merge_files(mine_lines, older_lines, yours_lines);

    for (const auto& line : merged) {
      safePrintLn(line);
    }
  } else if (cfg.ed_script) {
    // Output ed script format
    safePrintLn("# diff3 ed script");

    for (const auto& change : mine_older.changes) {
      safePrintLn(std::to_string(change.first + 1) + "," +
                  std::to_string(change.first + change.second) + "d");
    }

    for (const auto& change : older_yours.changes) {
      safePrintLn(std::to_string(change.first + 1) + "a");
      for (int i = 0; i < change.second; ++i) {
        safePrintLn(yours_lines[change.first + i]);
      }
      safePrintLn(".");
    }
  } else if (cfg.bracketed_conflicts) {
    // Output with bracketed conflicts
    std::vector<std::string> merged =
        merge_files(mine_lines, older_lines, yours_lines);

    for (const auto& line : merged) {
      safePrintLn(line);
    }
  } else {
    // Default output: show conflicts
    int conflict_count = 0;

    for (const auto& mine_change : mine_older.changes) {
      for (const auto& yours_change : older_yours.changes) {
        if (mine_change.first == yours_change.first) {
          conflict_count++;
          safePrintLn("====");
          safePrintLn(std::to_string(mine_change.first + 1) + ": " +
                      (mine_change.first < mine_lines.size()
                           ? mine_lines[mine_change.first]
                           : ""));
          safePrintLn(std::to_string(yours_change.first + 1) + ": " +
                      (yours_change.first < yours_lines.size()
                           ? yours_lines[yours_change.first]
                           : ""));
        }
      }
    }

    if (conflict_count == 0) {
      safePrintLn("No conflicts found");
    } else {
      safePrintLn(std::to_string(conflict_count) + " conflicts found");
    }
  }

  return 0;
}

}  // namespace diff3_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(diff3,
                 /* name */
                 "diff3",

                 /* synopsis */
                 "diff3 [OPTION] MINE OLDER YOURS",

                 /* description */
                 "Compare three files.\n"
                 "Compare three files line by line and report differences.\n"
                 "MINE is your file, OLDER is the common ancestor, YOURS is "
                 "the other file.\n"
                 "By default, outputs conflicts between MINE and YOURS.",

                 /* examples */
                 "  diff3 mine.c older.c yours.c\n"
                 "  diff3 -m mine.txt older.txt yours.txt\n"
                 "  diff3 -E file1.txt file2.txt file3.txt",

                 /* see_also */
                 "diff(1), sdiff(1), patch(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 DIFF3_OPTIONS) {
  using namespace diff3_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("diff3: ");
    safeErrorPrintLn(cfg_result.error());
    safePrintLn("Usage: diff3 [OPTION] MINE OLDER YOURS");
    return 1;
  }

  return run(*cfg_result);
}
