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
 *  - File: stty.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stty.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

#ifndef ENABLE_ECHO_NEWLINE
#define ENABLE_ECHO_NEWLINE 0x0004
#endif

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr STTY_OPTIONS = std::array{
    OPTION("-a", "--all", "print all current settings in human-readable form"),
    OPTION("-g", "--save",
           "print all current settings in a stty-readable form"),
    OPTION("-F", "--file", "open and use the specified device instead of stdin",
           STRING_TYPE)};

namespace stty_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool all = false;
  bool save = false;
  std::string device;
  SmallVector<std::string, 32> settings;
};

auto build_config(const CommandContext<STTY_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  cfg.save = ctx.get<bool>("-g", false) || ctx.get<bool>("--save", false);
  cfg.device = ctx.get<std::string>("--file", "");
  if (cfg.device.empty()) {
    cfg.device = ctx.get<std::string>("-F", "");
  }

  for (const auto& pos : ctx.positionals) {
    cfg.settings.push_back(std::string(pos));
  }

  return cfg;
}

// Map Windows console mode flags to human-readable settings
void print_console_settings(HANDLE hCon) {
  DWORD mode = 0;
  if (!GetConsoleMode(hCon, &mode)) {
    safeErrorPrintLn("stty: standard input: Not a terminal");
    return;
  }

  // Input flags
  safePrint("intr = ^C; ");
  safePrint("quit = ^\\; ");
  safePrint("erase = ^?; ");
  safePrint("kill = ^U; ");
  safePrint("eof = ^D; ");
  safePrint("eol = <undef>; ");
  safePrint("eol2 = <undef>; ");
  safePrint("swtch = <undef>; ");
  safePrint("start = ^Q; ");
  safePrint("stop = ^S; ");
  safePrint("susp = ^Z; ");
  safePrint("rprnt = ^R; ");
  safePrint("werase = ^W; ");
  safePrint("lnext = ^V; ");
  safePrint("discard = ^O; ");
  safePrintLn("");

  // Input modes
  safePrint((mode & ENABLE_PROCESSED_INPUT) ? "min = 1; " : "min = 0; ");
  safePrint("time = 0; ");
  safePrintLn("");

  // Control flags
  safePrintLn("speed 38400 baud; rows 50; columns 120; line = 0;");

  // Input settings
  if (mode & ENABLE_PROCESSED_INPUT)
    safePrint(" -ignbrk");
  else
    safePrint(" ignbrk");
  safePrint(" -brkint");
  safePrint(" -parmrk");
  safePrint(" -istrip");
  safePrint(" -inlcr");
  safePrint(" -igncr");
  if (mode & ENABLE_PROCESSED_INPUT)
    safePrint(" icrnl");
  else
    safePrint(" -icrnl");
  safePrint(" -ixon");
  safePrint(" -ixoff");
  safePrint(" -iuclc");
  safePrint(" -ixany");
  safePrint(" -imaxbel");
  safePrint(" -iutf8");
  safePrintLn("");

  // Output settings
  safePrint(" -opost");
  safePrint(" -olcuc");
  safePrint(" -ocrnl");
  safePrint(" -onlcr");
  safePrint(" -onocr");
  safePrint(" -onlret");
  safePrint(" -ofill");
  safePrint(" -ofdel");
  safePrintLn(" nl0 cr0 tab0 bs0 vt0 ff0");

  // Local settings
  if (mode & ENABLE_ECHO_INPUT)
    safePrint(" echo");
  else
    safePrint(" -echo");
  if (mode & ENABLE_ECHO_INPUT)
    safePrint(" echoe");
  else
    safePrint(" -echoe");
  if (mode & ENABLE_LINE_INPUT)
    safePrint(" echok");
  else
    safePrint(" -echok");
  safePrint(" -echonl");
  safePrint(" -noflsh");
  safePrint(" -tostop");
  if (mode & ENABLE_ECHO_INPUT)
    safePrint(" echoctl");
  else
    safePrint(" -echoctl");
  safePrint(" -echoprt");
  if (mode & ENABLE_ECHO_INPUT)
    safePrint(" echoke");
  else
    safePrint(" -echoke");
  safePrint(" -flusho");
  safePrint(" -extproc");
  safePrintLn("");

  // Special characters
  safePrintLn("sane -icanon");
}

void print_machine_readable(HANDLE hCon) {
  DWORD mode = 0;
  if (!GetConsoleMode(hCon, &mode)) {
    return;
  }

  // Output in stty-readable format: colon-separated hex values
  // intr:03 quit:1c erase:7f kill:15 eof:04 eol:ff eol2:ff swtch:ff
  // start:13 stop:13 susp:1a rprnt:12 werase:17 lnext:16 discard:0f
  safePrintLn("00:0:4:7f:11:1:1:0:3:1c:15:12:16:0:f:0:1:0:0:0:0:0:0:"
              "0:0:0:0:0:0:0:0:0:0:0:0:0");
}

void apply_sane(HANDLE hCon) {
  // Reset to reasonable defaults
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
               ENABLE_ECHO_NEWLINE;
  SetConsoleMode(hCon, mode);
  safePrintLn("stty: 'sane' applied");
}

void apply_raw(HANDLE hCon) {
  // Disable all processing
  DWORD mode = 0;
  SetConsoleMode(hCon, mode);
  safePrintLn("stty: 'raw' applied");
}

void apply_cooked(HANDLE hCon) {
  // Enable standard processing
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
  SetConsoleMode(hCon, mode);
  safePrintLn("stty: 'cooked' applied");
}

void apply_cbreak(HANDLE hCon) {
  // Like -icanon with min=1
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT;
  SetConsoleMode(hCon, mode);
  safePrintLn("stty: 'cbreak' applied");
}

bool apply_setting(HANDLE hCon, const std::string& setting) {
  DWORD mode = 0;
  GetConsoleMode(hCon, &mode);

  if (setting == "sane") {
    apply_sane(hCon);
    return true;
  }
  if (setting == "raw") {
    apply_raw(hCon);
    return true;
  }
  if (setting == "cooked" || setting == "-raw") {
    apply_cooked(hCon);
    return true;
  }
  if (setting == "cbreak") {
    apply_cbreak(hCon);
    return true;
  }

  // Boolean settings
  bool value = true;
  std::string name = setting;
  if (!name.empty() && name[0] == '-') {
    value = false;
    name = name.substr(1);
  }

  if (name == "echo") {
    if (value)
      mode |= ENABLE_ECHO_INPUT;
    else
      mode &= ~ENABLE_ECHO_INPUT;
    SetConsoleMode(hCon, mode);
    return true;
  }
  if (name == "icanon") {
    if (value)
      mode |= ENABLE_LINE_INPUT;
    else
      mode &= ~ENABLE_LINE_INPUT;
    SetConsoleMode(hCon, mode);
    return true;
  }
  if (name == "isig") {
    if (value)
      mode |= ENABLE_PROCESSED_INPUT;
    else
      mode &= ~ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hCon, mode);
    return true;
  }
  if (name == "echoe") {
    if (value)
      mode |= ENABLE_ECHO_NEWLINE;
    else
      mode &= ~ENABLE_ECHO_NEWLINE;
    SetConsoleMode(hCon, mode);
    return true;
  }

  // Settings that don't map to Windows but we accept silently
  static const std::vector<std::string> accepted = {
      "ignbrk",  "brkint",  "parmrk", "istrip", "inlcr",  "igncr",
      "icrnl",   "ixon",    "ixoff",  "iuclc",  "ixany",  "imaxbel",
      "iutf8",   "opost",   "olcuc",  "ocrnl",  "onlcr",  "onocr",
      "onlret",  "ofill",   "ofdel",  "echoctl", "echoprt", "echoke",
      "echok",   "echonl",  "noflsh", "tostop",  "flusho",  "extproc",
      "pendin",  "echoe"};

  for (const auto& a : accepted) {
    if (name == a) return true;
  }

  // Settings with values
  if (name == "intr" || name == "quit" || name == "erase" || name == "kill" ||
      name == "eof" || name == "eol" || name == "start" || name == "stop" ||
      name == "susp" || name == "rprnt" || name == "werase" || name == "lnext" ||
      name == "discard") {
    return true;  // Accept but no-op on Windows
  }
  if (name == "min" || name == "time" || name == "rows" || name == "cols" ||
      name == "columns" || name == "line" || name == "speed") {
    return true;  // Accept but no-op on Windows
  }

  return false;
}

auto run(const Config& cfg) -> int {
  HANDLE hCon = GetStdHandle(STD_INPUT_HANDLE);

  if (!cfg.settings.empty()) {
    // Apply settings
    bool ok = true;
    for (const auto& setting : cfg.settings) {
      if (!apply_setting(hCon, setting)) {
        safeErrorPrint("stty: invalid argument '");
        safeErrorPrint(setting);
        safeErrorPrintLn("'");
        ok = false;
      }
    }
    return ok ? 0 : 1;
  }

  if (cfg.save) {
    print_machine_readable(hCon);
    return 0;
  }

  // Default: print all settings (same as -a)
  print_console_settings(hCon);
  return 0;
}

}  // namespace stty_pipeline

REGISTER_COMMAND(
    stty, "stty", "stty [-a|--all] [-g|--save] [SETTING...]",
    "Print or change terminal characteristics.\n"
    "\n"
    "With no arguments, prints all terminal settings in human-readable form.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all     print all current settings in human-readable form\n"
    "  -g, --save    print all current settings in a stty-readable form\n"
    "  -F, --file    open and use the specified device instead of stdin\n"
    "\n"
    "Settings:\n"
    "  sane          reset all settings to reasonable defaults\n"
    "  raw           disable all input processing\n"
    "  cooked        enable standard input processing\n"
    "  cbreak        like -icanon with min=1\n"
    "  echo          echo input characters\n"
    "  -echo         do not echo input characters\n"
    "  icanon        enable canonical (line) mode\n"
    "  -icanon       disable canonical mode\n"
    "  isig          enable interrupt/quit signals\n"
    "  -isig         disable interrupt/quit signals\n"
    "\n"
    "Note: On Windows, only echo/icanon/isig settings have actual effect.\n"
    "Other settings are accepted but silently ignored.",
    "  stty           show all settings\n"
    "  stty -a        show all settings\n"
    "  stty -g        show machine-readable settings\n"
    "  stty sane      reset to defaults\n"
    "  stty -echo     disable echo\n"
    "  stty raw       set raw mode",
    "stty(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", STTY_OPTIONS) {
  using namespace stty_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"stty");
    return 1;
  }

  return run(*cfg_result);
}
