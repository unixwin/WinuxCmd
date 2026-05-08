/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: tee.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for tee.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief TEE command options definition
 *
 * This array defines all the options supported by the tee command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --append: Append to the given FILEs, do not overwrite
 * [IMPLEMENTED]
 * - @a -i, @a --ignore-interrupts: Ignore interrupt signals [NOT SUPPORT]
 * - @a -p, @a --diagnose: Write errors to standard error [NOT SUPPORT]
 */
auto constexpr TEE_OPTIONS = std::array{
    OPTION("-a", "--append", "append to the given FILEs, do not overwrite"),
    OPTION("-i", "--ignore-interrupts", "ignore interrupt signals"),
    OPTION("-p", "--diagnose", "write errors to standard error")};

REGISTER_COMMAND(
    tee, "tee",
    "read from standard input and write to standard output and files",
    "Copy standard input to each FILE, and also to standard output.\n"
    "\n"
    "If a FILE is -, copy to standard output.",
    "  echo 'Hello' | tee output.txt       Save output to file\n"
    "  echo 'World' | tee -a output.txt    Append to file\n"
    "  cat file.txt | tee backup.txt       Create backup while viewing",
    "cat(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", TEE_OPTIONS) {
  using namespace core::pipeline;

  bool append = ctx.get<bool>("-a", false) || ctx.get<bool>("--append", false);

  // Get output files from positional arguments
  SmallVector<std::string, 32> output_files;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto &file : glob_result.files) {
          output_files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    output_files.push_back(file_arg);
  }

  if (output_files.empty()) {
    // No files specified, just copy stdin to stdout
    std::string line;
    while (std::getline(std::cin, line)) {
      safePrintLn(line);
    }
    return 0;
  }

  // Open output files
  SmallVector<std::ofstream, 32> file_streams;
  for (const auto &filename : output_files) {
    if (filename == "-") {
      // "-" means stdout, skip opening
      continue;
    }

    std::ofstream file;
    if (append) {
      file.open(filename, std::ios::out | std::ios::app | std::ios::binary);
    } else {
      file.open(filename, std::ios::out | std::ios::trunc | std::ios::binary);
    }

    if (!file.is_open()) {
      safeErrorPrint("tee: '");
      safeErrorPrint(filename);
      safeErrorPrint("': ");
      safeErrorPrint(strerror(errno));
      safeErrorPrint("\n");
      return 1;
    }

    file_streams.push_back(std::move(file));
  }

  // Read from stdin and write to all outputs
  std::string line;
  while (std::getline(std::cin, line)) {
    // Write to stdout
    safePrintLn(line);

    // Write to all files
    for (auto &file : file_streams) {
      file << line << '\n';
      if (file.fail()) {
        safeErrorPrint("tee: write error\n");
        return 1;
      }
    }
  }

  // Close all files
  for (auto &file : file_streams) {
    file.close();
    if (file.fail()) {
      safeErrorPrint("tee: error closing file\n");
      return 1;
    }
  }

  return 0;
}
