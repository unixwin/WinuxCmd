// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for man command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr MAN_OPTIONS = std::array{
    // [EXT]
    OPTION("-l", "--list", "list all available commands"),
    // [GNU]
    OPTION("-w", "--where", "print physical location of manual page"),
    // [GNU]
    OPTION("-P", "--pager", "use program PAGER to display output", STRING_TYPE),
    // [GNU]
    OPTION("-t", "--troff",
           "use groff to format pages [DIFFERS: not available on Windows]"),
    // [GNU]
    OPTION(
        "-T", "--troff-device",
        "use groff with selected device [DIFFERS: not available on Windows]"),
    // [GNU]
    OPTION("-H", "--html",
           "display HTML output [DIFFERS: not available on Windows]"),
};

REGISTER_COMMAND(man,
                 /* cmd_name */ "man",
                 /* cmd_synopsis */ "man [OPTION]... [COMMAND]...",
                 /* cmd_desc */
                 "Display manual page for a WinuxCmd command.\n"
                 "[DIFFERS] Options -t, -T, -H are GNU-specific and not "
                 "available on Windows.",
                 /* examples */ "man ls\nman grep\nman --list\nman -w ls",
                 /* see_also */ "help(1)",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ MAN_OPTIONS) {
  bool list_mode = ctx.get<bool>("-l", false) || ctx.get<bool>("--list", false);
  bool where_mode =
      ctx.get<bool>("-w", false) || ctx.get<bool>("--where", false);
  bool troff_mode =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--troff", false);
  bool html_mode = ctx.get<bool>("-H", false) || ctx.get<bool>("--html", false);

  // [DIFFERS] -t/--troff, -T/--troff-device, -H/--html not available on Windows
  if (troff_mode) {
    safeErrorPrintLn("man: [DIFFERS] --troff is not available on Windows");
    return 1;
  }
  if (ctx.get<bool>("-T", false) || ctx.get<bool>("--troff-device", false)) {
    safeErrorPrintLn(
        "man: [DIFFERS] --troff-device is not available on Windows");
    return 1;
  }
  // -P/--pager: accepted but WinuxCmd uses its built-in pager [DIFFERS]
  (void)ctx.get<std::string>("-P", "");
  (void)ctx.get<std::string>("--pager", "");
  if (html_mode) {
    safeErrorPrintLn("man: [DIFFERS] --html is not available on Windows");
    return 1;
  }

  if (list_mode) {
    auto commands = CommandRegistry::getAllCommands();
    std::string output = "Available commands:\n";
    for (const auto &[name, desc] : commands) {
      output += "  ";
      output.append(name.data(), name.size());
      output += "  - ";
      output.append(desc.data(), desc.size());
      output += "\n";
    }
    return winux::pager::page_text(
        output, winux::pager::Options{.title = "man --list"});
  }

  if (where_mode) {
    if (ctx.positionals.empty()) {
      safeErrorPrintLn("man: --where requires a command name");
      return 1;
    }
    std::string cmd_name(ctx.positionals[0]);
    auto man_page = CommandRegistry::getManPage(cmd_name);
    if (man_page.empty()) {
      safeErrorPrintLn("man: no manual entry for " + cmd_name);
      return 1;
    }
    // WinuxCmd stores man pages in-memory; show a synthetic path
    safePrintLn("winuxcmd/man/" + cmd_name + ".txt");
    return 0;
  }

  if (ctx.positionals.empty()) {
    safePrintLn("Usage: man [OPTION]... [COMMAND]...");
    safePrintLn("Display manual page for a WinuxCmd command.");
    safePrintLn("");
    safePrintLn("Options:");
    safePrintLn("  -l, --list    list all available commands");
    safePrintLn("  -w, --where   print physical location of manual page");
    safePrintLn("  -P, --pager   use program PAGER to display output");
    safePrintLn("");
    safePrintLn("Examples:");
    safePrintLn("  man ls        Show manual page for ls");
    safePrintLn("  man --list    List all commands");
    safePrintLn("  man -w ls     Show location of ls manual page");
    return 0;
  }

  std::string cmd_name(ctx.positionals[0]);
  auto man_page = CommandRegistry::getManPage(cmd_name);

  if (man_page.empty()) {
    safePrintLn(L"man: no manual entry for " + utf8_to_wstring(cmd_name));
    safePrintLn(L"Try 'man --list' to see available commands.");
    return 1;
  }

  if (!man_page.ends_with('\n')) man_page += "\n";
  return winux::pager::page_text(
      man_page, winux::pager::Options{.title = "man " + cmd_name});
}
