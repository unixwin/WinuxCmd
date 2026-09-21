// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [EXT]
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
    std::ifstream input(native_path::normalize_api_operand_w(wfilename),
                        std::ios::binary);
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
    // Preserve stdin byte-for-byte except for CRLF pairs. getline() would
    // manufacture a final newline for input that does not have one.
    std::string content((std::istreambuf_iterator<char>(std::cin)), {});
    size_t pos = 0;
    while ((pos = content.find("\r\n", pos)) != std::string::npos) {
      content.erase(pos, 1);
    }
    safePrint(content);
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
