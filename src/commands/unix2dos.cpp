// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unix2dos command.
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

auto constexpr UNIX2DOS_OPTIONS = std::array{
    // [EXT] option
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(unix2dos,
                 /* name */
                 "unix2dos",

                 /* synopsis */
                 "unix2dos [OPTION]... [FILE]...",
                 "Convert Unix line endings to DOS line endings.\n"
                 "\n"
                 "Replace LF (\\n) with CRLF (\\r\\n) in each specified file.\n"
                 "This is an alias for the u2d command.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  unix2dos file.txt\n"
                 "  unix2dos -v *.txt\n"
                 "  cat unix_file.txt | unix2dos > dos_file.txt",

                 /* see also */
                 "dos2unix(1), d2u(1), u2d(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", UNIX2DOS_OPTIONS) {
  namespace cp = core::pipeline;

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);

  auto process_file = [&](const std::string& filename,
                          bool modify_in_place) -> bool {
    std::wstring wfilename = utf8_to_wstring(filename);
    std::ifstream input(wfilename, std::ios::binary);
    if (!input) {
      safeErrorPrintLn("unix2dos: cannot open '" + filename +
                       "': No such file or directory");
      return false;
    }

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    input.close();

    // Convert LF to CRLF (but avoid double conversion)
    size_t pos = 0;
    while ((pos = content.find("\n", pos)) != std::string::npos) {
      // Check if this is already CRLF
      if (pos > 0 && content[pos - 1] == '\r') {
        pos++;  // Skip, already CRLF
      } else {
        content.insert(pos, 1, '\r');
        pos += 2;
      }
    }

    if (modify_in_place) {
      // Write back to file
      std::ofstream output(wfilename, std::ios::binary);
      if (!output) {
        safeErrorPrintLn("unix2dos: cannot write to '" + filename + "'");
        return false;
      }
      output.write(content.data(), content.size());
      output.close();

      if (verbose) {
        safePrintLn("unix2dos: converted '" + filename + "'");
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
      if (content[i] == '\n' && (i == 0 || content[i - 1] != '\r'))
        safePrint("\r");
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
