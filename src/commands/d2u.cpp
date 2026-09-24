// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for d2u command.
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

auto constexpr D2U_OPTIONS = std::array{
    // [EXT]
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(d2u,
                 /* name */
                 "d2u",

                 /* synopsis */
                 "d2u [OPTION]... [FILE]...",
                 "Convert DOS line endings to Unix line endings.\n"
                 "\n"
                 "Replace CRLF (\\r\\n) with LF (\\n) in each specified file.\n"
                 "If no FILE is specified, read from standard input and write "
                 "to standard output.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  d2u file.txt\n"
                 "  d2u -v *.txt\n"
                 "  cat dos_file.txt | d2u > unix_file.txt",

                 /* see also */
                 "u2d(1), unix2dos(1), dos2unix(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", D2U_OPTIONS) {
  namespace cp = core::pipeline;

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);

  auto process_file = [&](const std::string& filename,
                          bool modify_in_place) -> bool {
    std::wstring wfilename = utf8_to_wstring(filename);
    std::ifstream input(native_path::normalize_api_operand_w(wfilename),
                        std::ios::binary);
    if (!input) {
      safeErrorPrintLn("d2u: cannot open '" + filename +
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
        safeErrorPrintLn("d2u: cannot write to '" + filename + "'");
        return false;
      }
      output.write(content.data(), content.size());
      output.close();

      if (verbose) {
        safePrintLn("d2u: converted '" + filename + "'");
      }
    } else {
      // Write to stdout
      safePrint(content);
    }

    return true;
  };

  if (ctx.positionals.empty()) {
    std::string content((std::istreambuf_iterator<char>(std::cin)), {});
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == '\r' && i + 1 < content.size() &&
          content[i + 1] == '\n')
        continue;
      safePrint(content.substr(i, 1));
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
