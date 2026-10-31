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
    // [GNU] -a, --all: display all matching manual pages
    OPTION("-a", "--all", "display all matching manual pages"),
    // [GNU] -k, --apropos: search the manual page names and descriptions
    OPTION("-k", "--apropos", "search the manual page names and descriptions"),
    // [GNU] -7, --ascii: display ASCII translation of certain characters
    OPTION("-7", "--ascii", "display ASCII translation of certain characters"),
    // [GNU] --catman: create or update the pre-formatted manual pages
    OPTION("", "--catman",
           "create or update the pre-formatted manual pages [DIFFERS: not "
           "available on Windows]"),
    // [GNU] -C, --config-file: use this user configuration file
    OPTION("-C", "--config-file", "use this user configuration file",
           STRING_TYPE),
    // [GNU] -d, --debug: emit debugging messages
    OPTION("-d", "--debug", "emit debugging messages"),
    // [GNU] -D, --default: reset options to their default values
    OPTION("-D", "--default", "reset options to their default values"),
    // [GNU]
    OPTION("-P", "--pager", "use program PAGER to display output", STRING_TYPE),
    // [GNU]
    OPTION("-t", "--troff",
           "use groff to format pages [DIFFERS: not available on Windows]"),
    // [GNU]
    OPTION(
        "-T", "--troff-device",
        "use groff with selected device [DIFFERS: not available on Windows]"),
    // [GNU] -Z, --ditroff: groff with -Tps output
    OPTION("-Z", "--ditroff",
           "use groff and force ps output [DIFFERS: not available on "
           "Windows]"),
    // [GNU]
    OPTION("-H", "--html",
           "display HTML output [DIFFERS: not available on Windows]"),
};

REGISTER_COMMAND(man,
                 /* cmd_name */ "man",
                 /* cmd_synopsis */ "man [OPTION]... [COMMAND]...",
                 /* cmd_desc */
                 "man is the manual pager for WinuxCmd.  Each page argument "
                 "given to\n"
                 "man is normally the name of a command.  WinuxCmd ships one "
                 "manual\n"
                 "page per supported command in a built-in registry; the "
                 "requested\n"
                 "page is formatted and displayed through the built-in "
                 "pager.\n"
                 "\n"
                 "The default action is to display the manual page in the "
                 "terminal;\n"
                 "every page argument is shown in turn.  With -l/--list, "
                 "man lists\n"
                 "every command that has a manual page and exits.  With "
                 "-w/--where,\n"
                 "man prints the physical location of each requested page "
                 "instead of\n"
                 "displaying it.  With -k/--apropos, each argument is a "
                 "keyword and\n"
                 "man prints the registered pages whose name or description "
                 "contains\n"
                 "it.  -C/--config-file names a configuration file that "
                 "must exist;\n"
                 "its settings do not apply to the built-in registry.  "
                 "-d/--debug\n"
                 "traces page lookups on standard error.  -7/--ascii and "
                 "-D/--default\n"
                 "are accepted; the built-in pages are already plain ASCII "
                 "text.\n"
                 "\n"
                 "Mandatory or optional arguments to long options are also "
                 "mandatory\n"
                 "or optional for any corresponding short options.\n"
                 "\n"
                 "[DIFFERS] man-db options that need a system man tree or "
                 "external\n"
                 "formatters are not implemented: section selection,\n"
                 "-f/--whatis, -K/--global-apropos,\n"
                 "-l/--local-file (WinuxCmd rebinds -l to --list),\n"
                 "--regex, --wildcard, --names-only, -M/--manpath,\n"
                 "-m/--systems, -S/--sections, -e/--extension, -i/-I case "
                 "control,\n"
                 "-L/--locale, -u/--update, -r/--prompt,\n"
                 "-E/--encoding, --nh, --nj, -p/--preprocessor,\n"
                 "-X/--gxditview and --usage.  -P/--pager is\n"
                 "accepted; the built-in pager is always used.  -t/--troff,\n"
                 "-T/--troff-device, -H/--html, -Z/--ditroff and --catman "
                 "report an error.",
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
  bool apropos_mode =
      ctx.get<bool>("-k", false) || ctx.get<bool>("--apropos", false);
  bool debug_mode =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--debug", false);

  // [DIFFERS] -t/--troff, -T/--troff-device, -H/--html, -Z/--ditroff and
  // --catman are not available on Windows.
  if (troff_mode) {
    safeErrorPrintLn("man: [DIFFERS] --troff is not available on Windows");
    return 1;
  }
  if (ctx.get<bool>("-T", false) || ctx.get<bool>("--troff-device", false)) {
    safeErrorPrintLn(
        "man: [DIFFERS] --troff-device is not available on Windows");
    return 1;
  }
  if (html_mode) {
    safeErrorPrintLn("man: [DIFFERS] --html is not available on Windows");
    return 1;
  }
  if (ctx.get<bool>("-Z", false) || ctx.get<bool>("--ditroff", false)) {
    safeErrorPrintLn("man: [DIFFERS] --ditroff is not available on Windows");
    return 1;
  }
  if (ctx.get<bool>("--catman", false)) {
    safeErrorPrintLn("man: [DIFFERS] --catman is not available on Windows");
    return 1;
  }

  // -C/--config-file: the file must exist (man-db errors out otherwise);
  // its contents do not apply to the built-in registry.
  std::string config_file = ctx.get<std::string>("-C", "");
  if (config_file.empty()) {
    config_file = ctx.get<std::string>("--config-file", "");
  }
  if (!config_file.empty()) {
    if (debug_mode) {
      safeErrorPrintLn("Reading config file " + config_file);
    }
    std::error_code ec;
    if (!std::filesystem::exists(
            native_path::normalize_api_operand(config_file), ec)) {
      safeErrorPrintLn("man: cannot open config file '" + config_file + "'");
      return 1;
    }
  }

  // -P/--pager: accepted but WinuxCmd uses its built-in pager [DIFFERS]
  (void)ctx.get<std::string>("-P", "");
  (void)ctx.get<std::string>("--pager", "");
  // -7/--ascii and -D/--default: accepted; pages are plain ASCII text
  // and the port keeps no persistent option state to reset.
  (void)(ctx.get<bool>("-7", false) || ctx.get<bool>("--ascii", false));
  (void)(ctx.get<bool>("-D", false) || ctx.get<bool>("--default", false));
  // -a/--all: the registry holds exactly one page per name, so showing
  // every operand in turn already covers the "all" semantics.
  (void)(ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false));

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

  if (apropos_mode) {
    if (ctx.positionals.empty()) {
      safeErrorPrintLn("man: --apropos requires a keyword");
      return 1;
    }
    auto commands = CommandRegistry::getAllCommands();
    bool any_match = false;
    for (const auto &word_view : ctx.positionals) {
      const std::string word(word_view);
      const std::string needle = ascii_lower_copy(word);
      bool matched = false;
      for (const auto &[name, desc] : commands) {
        const std::string haystack =
            ascii_lower_copy(std::string(name) + " " + std::string(desc));
        if (haystack.find(needle) != std::string::npos) {
          safePrintLn(std::string(name) + " - " + std::string(desc));
          matched = true;
          any_match = true;
        }
      }
      if (!matched) {
        safePrintLn(word + ": nothing appropriate.");
      }
    }
    return any_match ? 0 : 1;
  }

  if (where_mode) {
    if (ctx.positionals.empty()) {
      safeErrorPrintLn("man: --where requires a command name");
      return 1;
    }
    int rc = 0;
    for (const auto &name_view : ctx.positionals) {
      const std::string cmd_name(name_view);
      if (debug_mode) {
        safeErrorPrintLn("man: [debug] looking for page '" + cmd_name + "'");
      }
      auto man_page = CommandRegistry::getManPage(cmd_name);
      if (man_page.empty()) {
        safeErrorPrintLn("man: no manual entry for " + cmd_name);
        rc = 1;
        continue;
      }
      // WinuxCmd stores man pages in-memory; show a synthetic path
      safePrintLn("winuxcmd/man/" + cmd_name + ".txt");
    }
    return rc;
  }

  if (ctx.positionals.empty()) {
    safePrintLn("Usage: man [OPTION]... [COMMAND]...");
    safePrintLn("Display manual page for a WinuxCmd command.");
    safePrintLn("");
    safePrintLn("Options:");
    safePrintLn("  -l, --list    list all available commands");
    safePrintLn("  -w, --where   print physical location of manual page");
    safePrintLn("  -a, --all     display all matching manual pages");
    safePrintLn("  -k, --apropos search page names and descriptions");
    safePrintLn("  -C, --config-file=FILE");
    safePrintLn("                use this user configuration file");
    safePrintLn("  -d, --debug   emit debugging messages");
    safePrintLn("  -7, --ascii   display ASCII translation of characters");
    safePrintLn("  -P, --pager   use program PAGER to display output");
    safePrintLn("");
    safePrintLn("Examples:");
    safePrintLn("  man ls        Show manual page for ls");
    safePrintLn("  man --list    List all commands");
    safePrintLn("  man -w ls     Show location of ls manual page");
    return 0;
  }

  int rc = 0;
  for (const auto &name_view : ctx.positionals) {
    const std::string cmd_name(name_view);
    if (debug_mode) {
      safeErrorPrintLn("man: [debug] looking for page '" + cmd_name + "'");
    }
    auto man_page = CommandRegistry::getManPage(cmd_name);
    if (man_page.empty()) {
      if (debug_mode) {
        safeErrorPrintLn("man: [debug] no page found for '" + cmd_name + "'");
      }
      safePrintLn(L"man: no manual entry for " + utf8_to_wstring(cmd_name));
      safePrintLn(L"Try 'man --list' to see available commands.");
      rc = 1;
      continue;
    }
    if (debug_mode) {
      safeErrorPrintLn("man: [debug] found page '" + cmd_name + "'");
    }
    if (!man_page.ends_with('\n')) man_page += "\n";
    const int page_rc = winux::pager::page_text(
        man_page, winux::pager::Options{.title = "man " + cmd_name});
    if (page_rc != 0) rc = page_rc;
  }
  return rc;
}
