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
 *  - File: dos2unix.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dos2unix command.
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

auto constexpr DOS2UNIX_OPTIONS = std::array{
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(dos2unix,
                 /* name */
                 "dos2unix",

                 /* synopsis */
                 "dos2unix [OPTION]... [FILE]...",
                 "Convert DOS line endings to Unix line endings.\n"
                 "\n"
                 "Replace CRLF (\\r\\n) with LF (\\n) in each specified file.\n"
                 "This is an alias for the d2u command.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  dos2unix file.txt\n"
                 "  dos2unix -v *.txt\n"
                 "  cat dos_file.txt | dos2unix > unix_file.txt",

                 /* see also */
                 "unix2dos(1), d2u(1), u2d(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", DOS2UNIX_OPTIONS) {
  namespace cp = core::pipeline;

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);

  auto process_file = [&](const std::string& filename,
                          bool modify_in_place) -> bool {
    std::wstring wfilename = utf8_to_wstring(filename);
    std::ifstream input(wfilename, std::ios::binary);
    if (!input) {
      safeErrorPrintLn("dos2unix: cannot open '" + filename +
                       "': No such file or directory");
      return false;
    }

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    input.close();

    // Convert CRLF to LF
    size_t pos = 0;
    while ((pos = content.find("\r\n", pos)) != std::string::npos) {
      content.erase(pos, 1);  // Remove \r, keep \n
    }

    if (modify_in_place) {
      // Write back to file
      std::ofstream output(wfilename, std::ios::binary);
      if (!output) {
        safeErrorPrintLn("dos2unix: cannot write to '" + filename + "'");
        return false;
      }
      output.write(content.data(), content.size());
      output.close();

      if (verbose) {
        safePrintLn("dos2unix: converted '" + filename + "'");
      }
    } else {
      // Write to stdout
      safePrint(content);
    }

    return true;
  };

  if (ctx.positionals.empty()) {
    // Read from stdin, write to stdout
    std::string line;
    bool first = true;
    while (std::getline(std::cin, line)) {
      // Remove trailing \r if present
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!first) {
        safePrint("\n");
      }
      safePrint(line);
      first = false;
    }
    if (!first) {
      safePrint("\n");
    }
  } else {
    // Process each file in place
    bool all_ok = true;
    for (auto file : ctx.positionals) {
      std::string file_arg(file);
      std::vector<std::string> expanded;
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& f : glob_result.files) {
            expanded.push_back(wstring_to_utf8(f));
          }
        } else {
          expanded.push_back(file_arg);
        }
      } else {
        expanded.push_back(file_arg);
      }
      for (const auto& exp : expanded) {
        if (!process_file(exp, true)) {
          all_ok = false;
        }
      }
    }
    return all_ok ? 0 : 1;
  }

  return 0;
}
