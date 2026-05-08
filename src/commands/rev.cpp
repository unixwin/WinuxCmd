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
 *  - File: rev.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for rev command.
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

auto constexpr REV_OPTIONS =
    std::array{OPTION("", "", "reverse lines of a file", STRING_TYPE)};

REGISTER_COMMAND(
    rev_cmd,
    /* name */
    "rev",

    /* synopsis */
    "rev [FILE]...",
    "Reverse lines characterwise.\n"
    "\n"
    "Print each specified line in reverse order, character by character.\n"
    "If no FILE is specified, read from standard input.",
    "  echo 'hello' | rev\n"
    "  rev input.txt\n"
    "  rev file1.txt file2.txt",

    /* see also */
    "tac(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", REV_OPTIONS) {
  std::vector<std::string> files;
  for (const auto& arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    files.push_back(file_arg);
  }

  std::function<void(std::istream&)> process_stream = [](std::istream& is) {
    std::string line;
    while (std::getline(is, line)) {
      // Handle CRLF line endings
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      std::reverse(line.begin(), line.end());
      safePrintLn(line);
    }
  };

  if (files.empty()) {
    // Read from stdin
    process_stream(std::cin);
  } else {
    for (const auto& file : files) {
      auto wfile = utf8_to_wstring(file);
      std::ifstream ifs(wfile, std::ios::binary);
      if (!ifs) {
        safeErrorPrintLn("rev: cannot open '" + file +
                         "': No such file or directory");
        return 1;
      }
      process_stream(ifs);
      ifs.close();
    }
  }

  return 0;
}
