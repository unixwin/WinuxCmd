// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    std::array{OPTION("-e", "", "output ed script"),
               // [GNU]
               OPTION("-E", "", "output ed script with bracketed conflicts"),
               // [GNU]
               OPTION("-m", "", "output in merged format"),
               // [GNU]
               OPTION("-A", "", "like -E, but overwrite overlapping changes"),
               // [GNU]
               OPTION("-a", "", "treat all files as text")};

// ======================================================
// Helper functions
// ======================================================

namespace {
bool diff3_file_exists_for_read(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(std::filesystem::u8path(path), ec) &&
         !std::filesystem::is_directory(std::filesystem::u8path(path), ec);
}

bool diff3_same_lines(const std::vector<std::string>& lhs,
                      const std::vector<std::string>& rhs) {
  return lhs == rhs;
}

struct Diff3Block {
  size_t start = 0;
  std::vector<std::string> mine;
  std::vector<std::string> older;
  std::vector<std::string> yours;
  std::vector<std::string> prefix;
  std::vector<std::string> suffix;
};

std::optional<Diff3Block> make_single_block(
    const std::vector<std::string>& mine, const std::vector<std::string>& older,
    const std::vector<std::string>& yours) {
  if (mine == older && older == yours) return std::nullopt;

  size_t prefix = 0;
  const size_t min_size = std::min({mine.size(), older.size(), yours.size()});
  while (prefix < min_size && mine[prefix] == older[prefix] &&
         older[prefix] == yours[prefix]) {
    ++prefix;
  }

  size_t suffix = 0;
  while (suffix < mine.size() - prefix && suffix < older.size() - prefix &&
         suffix < yours.size() - prefix &&
         mine[mine.size() - 1 - suffix] == older[older.size() - 1 - suffix] &&
         older[older.size() - 1 - suffix] == yours[yours.size() - 1 - suffix]) {
    ++suffix;
  }

  auto slice = [](const std::vector<std::string>& lines, size_t first,
                  size_t last_exclusive) {
    if (first >= last_exclusive) return std::vector<std::string>{};
    return std::vector<std::string>(
        lines.begin() + static_cast<std::ptrdiff_t>(first),
        lines.begin() + static_cast<std::ptrdiff_t>(last_exclusive));
  };

  Diff3Block block;
  block.start = prefix;
  block.prefix = slice(mine, 0, prefix);
  block.suffix = slice(mine, mine.size() - suffix, mine.size());
  block.mine = slice(mine, prefix, mine.size() - suffix);
  block.older = slice(older, prefix, older.size() - suffix);
  block.yours = slice(yours, prefix, yours.size() - suffix);
  return block;
}

std::string change_spec(size_t start, size_t count) {
  if (count == 0) {
    return std::to_string(start) + "a";
  }
  if (count == 1) {
    return std::to_string(start + 1) + "c";
  }
  return std::to_string(start + 1) + "," + std::to_string(start + count) + "c";
}

void print_default_section(int file_number, size_t start,
                           const std::vector<std::string>& lines,
                           bool diff3_print_lines) {
  safePrintLn(std::to_string(file_number) + ":" +
              change_spec(start, lines.size()));
  if (!diff3_print_lines) return;
  for (const auto& line : lines) {
    safePrintLn("  " + line);
  }
}

void diff3_print_lines(const std::vector<std::string>& lines) {
  for (const auto& line : lines) safePrintLn(line);
}

void output_default_block(const Diff3Block& block) {
  const bool mine_eq_older = diff3_same_lines(block.mine, block.older);
  const bool yours_eq_older = diff3_same_lines(block.yours, block.older);
  const bool mine_eq_yours = diff3_same_lines(block.mine, block.yours);

  if (mine_eq_older && !yours_eq_older) {
    safePrintLn("====3");
    print_default_section(1, block.start, block.mine, false);
    print_default_section(2, block.start, block.older, true);
    print_default_section(3, block.start, block.yours, true);
    return;
  }

  if (yours_eq_older && !mine_eq_older) {
    safePrintLn("====1");
    print_default_section(1, block.start, block.mine, true);
    print_default_section(2, block.start, block.older, false);
    print_default_section(3, block.start, block.yours, true);
    return;
  }

  if (mine_eq_yours && !mine_eq_older) {
    safePrintLn("====2");
    print_default_section(1, block.start, block.mine, true);
    print_default_section(3, block.start, block.yours, true);
    print_default_section(2, block.start, block.older, true);
    return;
  }

  safePrintLn("====");
  print_default_section(1, block.start, block.mine, true);
  print_default_section(2, block.start, block.older, true);
  print_default_section(3, block.start, block.yours, true);
}

void output_ed_script_block(const Diff3Block& block) {
  const bool mine_eq_older = diff3_same_lines(block.mine, block.older);
  const bool yours_eq_older = diff3_same_lines(block.yours, block.older);

  // GNU -e applies only non-overlapping OLDER-to-YOURS changes to MINE.
  if (mine_eq_older && !yours_eq_older) {
    safePrintLn(change_spec(block.start, block.mine.size()));
    diff3_print_lines(block.yours);
    safePrintLn(".");
  }
}

bool output_merge_block(const Diff3Block& block, const std::string& mine_file,
                        const std::string& older_file,
                        const std::string& yours_file) {
  diff3_print_lines(block.prefix);

  const bool mine_eq_older = diff3_same_lines(block.mine, block.older);
  const bool yours_eq_older = diff3_same_lines(block.yours, block.older);
  const bool mine_eq_yours = diff3_same_lines(block.mine, block.yours);

  bool conflict = false;
  if (mine_eq_older && !yours_eq_older) {
    diff3_print_lines(block.yours);
  } else if (yours_eq_older && !mine_eq_older) {
    diff3_print_lines(block.mine);
  } else if (mine_eq_yours && !mine_eq_older) {
    conflict = true;
    safePrintLn("<<<<<<< " + older_file);
    diff3_print_lines(block.older);
    safePrintLn("=======");
    diff3_print_lines(block.yours);
    safePrintLn(">>>>>>> " + yours_file);
  } else {
    conflict = true;
    safePrintLn("<<<<<<< " + mine_file);
    diff3_print_lines(block.mine);
    safePrintLn("||||||| " + older_file);
    diff3_print_lines(block.older);
    safePrintLn("=======");
    diff3_print_lines(block.yours);
    safePrintLn(">>>>>>> " + yours_file);
  }

  diff3_print_lines(block.suffix);
  return conflict;
}
}  // namespace

// ======================================================
// Pipeline components// ======================================================
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

  if (files.empty()) {
    return std::unexpected("missing operand");
  }
  if (files.size() < 3) {
    return std::unexpected("missing operand after '" + files.back() + "'");
  }
  if (files.size() > 3) {
    return std::unexpected("extra operand '" + files[3] + "'");
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
  std::vector<std::string> mine_lines = read_file_lines(cfg.mine_file);
  std::vector<std::string> older_lines = read_file_lines(cfg.older_file);
  std::vector<std::string> yours_lines = read_file_lines(cfg.yours_file);

  if (mine_lines.empty() && !diff3_file_exists_for_read(cfg.mine_file)) {
    safeErrorPrintLn("diff3: cannot read file '" + cfg.mine_file + "'");
    return 1;
  }
  if (older_lines.empty() && !diff3_file_exists_for_read(cfg.older_file)) {
    safeErrorPrintLn("diff3: cannot read file '" + cfg.older_file + "'");
    return 1;
  }
  if (yours_lines.empty() && !diff3_file_exists_for_read(cfg.yours_file)) {
    safeErrorPrintLn("diff3: cannot read file '" + cfg.yours_file + "'");
    return 1;
  }

  auto block = make_single_block(mine_lines, older_lines, yours_lines);

  if (cfg.merged_output) {
    if (!block) {
      diff3_print_lines(mine_lines);
      return 0;
    }
    return output_merge_block(*block, cfg.mine_file, cfg.older_file,
                              cfg.yours_file)
               ? 1
               : 0;
  }

  if (cfg.ed_script) {
    if (block) output_ed_script_block(*block);
    return 0;
  }

  if (cfg.bracketed_conflicts || cfg.overwrite_overlapping) {
    // Compatibility placeholder: these GNU modes are still modeled as default
    // reports until their specialized output is implemented.
  }

  if (block) output_default_block(*block);
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
    safeErrorPrint("Try 'diff3 --help' for more information.\n");
    return 1;
  }

  return run(*cfg_result);
}
