// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr STTY_OPTIONS = std::array{
    // [DIFFERS] -a, --all
    OPTION("-a", "--all", "print all current settings in human-readable form"),
    // [DIFFERS] -g, --save
    OPTION("-g", "--save",
           "print all current settings in a stty-readable form"),
    // [DIFFERS] -F, --file
    OPTION("-F", "--file", "open and use the specified device instead of stdin",
           STRING_TYPE),
    // [GNU] hidden developer option, spelled "---debug" on the command
    // line (stty.c {"-debug"}). Accepted and ignored: its only effect in
    // GNU is dumping termios bytes when tcsetattr cannot apply a mode,
    // which has no Windows console equivalent.
    OPTION("", "---debug", "", BOOL_TYPE)};

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
  // [GNU] echoe is not directly supported on Windows; always show as disabled
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
  safePrintLn(
      "00:0:4:7f:11:1:1:0:3:1c:15:12:16:0:f:0:1:0:0:0:0:0:0:"
      "0:0:0:0:0:0:0:0:0:0:0:0:0");
}

bool apply_sane(HANDLE hCon) {
  // Reset to reasonable defaults
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
               ENABLE_ECHO_NEWLINE;
  return SetConsoleMode(hCon, mode) != 0;
}

bool apply_raw(HANDLE hCon) {
  // Disable all processing
  DWORD mode = 0;
  return SetConsoleMode(hCon, mode) != 0;
}

bool apply_cooked(HANDLE hCon) {
  // Enable standard processing
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
  return SetConsoleMode(hCon, mode) != 0;
}

bool apply_cbreak(HANDLE hCon) {
  // Like -icanon with min=1
  DWORD mode = ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT;
  return SetConsoleMode(hCon, mode) != 0;
}

void print_try_help() {
  safeErrorPrint("Try " + std::string(1, static_cast<char>(39)) +
                 "stty --help" + std::string(1, static_cast<char>(39)) +
                 " for more information.\n");
}

auto normalized_setting_name(const std::string& setting) -> std::string {
  if (setting.starts_with("-") && setting != "-") {
    return setting.substr(1);
  }
  return setting;
}

auto is_value_setting(const std::string& name) -> bool {
  static const std::vector<std::string> names = {
      "intr",    "quit",    "erase",  "kill",  "eof",   "eol",   "eol2",
      "swtch",   "start",   "stop",   "susp",  "dsusp", "rprnt", "werase",
      "lnext",   "discard", "status", "min",   "time",  "rows",  "cols",
      "columns", "line",    "ispeed", "ospeed"};
  for (const auto& candidate : names) {
    if (name == candidate) return true;
  }
  return false;
}

auto is_decimal_token(const std::string& setting) -> bool {
  if (setting.empty()) return false;
  for (unsigned char ch : setting) {
    if (!std::isdigit(ch)) return false;
  }
  return true;
}

auto is_known_noarg_setting(const std::string& name) -> bool {
  static const std::vector<std::string> names = {
      "sane",    "raw",     "cooked", "cbreak",  "ek",       "evenp",
      "parity",  "oddp",    "nl",     "pass8",   "litout",   "decctlq",
      "tabs",    "lcase",   "LCASE",  "crt",     "dec",      "speed",
      "size",    "parenb",  "parodd", "cs5",     "cs6",      "cs7",
      "cs8",     "hupcl",   "hup",    "cstopb",  "cread",    "clocal",
      "crtscts", "ignbrk",  "brkint", "ignpar",  "parmrk",   "inpck",
      "istrip",  "inlcr",   "igncr",  "icrnl",   "ixon",     "ixoff",
      "tandem",  "iuclc",   "ixany",  "imaxbel", "iutf8",    "opost",
      "olcuc",   "ocrnl",   "onlcr",  "onocr",   "onlret",   "ofill",
      "ofdel",   "nl0",     "nl1",    "cr0",     "cr1",      "cr2",
      "cr3",     "tab0",    "tab1",   "tab2",    "tab3",     "bs0",
      "bs1",     "vt0",     "vt1",    "ff0",     "ff1",      "isig",
      "icanon",  "iexten",  "echo",   "echoe",   "crterase", "echok",
      "echonl",  "noflsh",  "xcase",  "tostop",  "echoprt",  "prterase",
      "echoctl", "ctlecho", "echoke", "crtkill", "flusho",   "extproc",
      "pendin"};
  for (const auto& candidate : names) {
    if (name == candidate) return true;
  }
  return false;
}

auto validate_settings(const SmallVector<std::string, 32>& settings) -> bool {
  for (size_t i = 0; i < settings.size(); ++i) {
    const auto& setting = settings[i];
    const auto name = normalized_setting_name(setting);
    if (is_value_setting(name)) {
      if (i + 1 >= settings.size()) {
        safeErrorPrint("stty: missing argument to " +
                       std::string(1, static_cast<char>(39)));
        safeErrorPrint(name);
        safeErrorPrintLn(std::string(1, static_cast<char>(39)));
        print_try_help();
        return false;
      }
      ++i;
      continue;
    }
    if (is_known_noarg_setting(name) || is_decimal_token(setting)) {
      continue;
    }
    safeErrorPrint("stty: invalid argument " +
                   std::string(1, static_cast<char>(39)));
    safeErrorPrint(setting);
    safeErrorPrintLn(std::string(1, static_cast<char>(39)));
    print_try_help();
    return false;
  }
  return true;
}

auto stdin_is_console(HANDLE hCon) -> bool {
  DWORD mode = 0;
  return GetConsoleMode(hCon, &mode) != 0;
}

auto report_inappropriate_ioctl(const Config& cfg) -> int {
  safeErrorPrint("stty: " + std::string(1, static_cast<char>(39)));
  if (cfg.device.empty()) {
    safeErrorPrint("standard input");
  } else {
    safeErrorPrint(cfg.device);
  }
  safeErrorPrintLn(std::string(1, static_cast<char>(39)) +
                   ": Inappropriate ioctl for device");
  return 1;
}

auto report_missing_device(const std::string& device) -> int {
  safeErrorPrint("stty: " + device + ": No such file or directory\n");
  return 1;
}
bool apply_setting(HANDLE hCon, const std::string& setting) {
  DWORD mode = 0;
  GetConsoleMode(hCon, &mode);

  if (setting == "sane") {
    return apply_sane(hCon);
  }
  if (setting == "raw") {
    return apply_raw(hCon);
  }
  if (setting == "cooked" || setting == "-raw") {
    return apply_cooked(hCon);
  }
  if (setting == "cbreak") {
    return apply_cbreak(hCon);
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
    return SetConsoleMode(hCon, mode) != 0;
  }
  if (name == "icanon") {
    if (value)
      mode |= ENABLE_LINE_INPUT;
    else
      mode &= ~ENABLE_LINE_INPUT;
    return SetConsoleMode(hCon, mode) != 0;
  }
  if (name == "isig") {
    if (value)
      mode |= ENABLE_PROCESSED_INPUT;
    else
      mode &= ~ENABLE_PROCESSED_INPUT;
    return SetConsoleMode(hCon, mode) != 0;
  }
  // Settings that don't map to Windows but we accept silently
  static const std::vector<std::string> accepted = {
      "ignbrk",  "brkint",  "parmrk", "istrip", "inlcr",   "igncr",  "icrnl",
      "ixon",    "ixoff",   "iuclc",  "ixany",  "imaxbel", "iutf8",  "opost",
      "olcuc",   "ocrnl",   "onlcr",  "onocr",  "onlret",  "ofill",  "ofdel",
      "echoctl", "echoprt", "echoke", "echok",  "echonl",  "noflsh", "tostop",
      "flusho",  "extproc", "pendin", "echoe"};

  for (const auto& a : accepted) {
    if (name == a) return true;
  }

  // Settings with values
  if (name == "intr" || name == "quit" || name == "erase" || name == "kill" ||
      name == "eof" || name == "eol" || name == "start" || name == "stop" ||
      name == "susp" || name == "rprnt" || name == "werase" ||
      name == "lnext" || name == "discard") {
    return true;  // Accept but no-op on Windows
  }
  if (name == "min" || name == "time" || name == "rows" || name == "cols" ||
      name == "columns" || name == "line" || name == "speed") {
    return true;  // Accept but no-op on Windows
  }

  return is_known_noarg_setting(name) || is_value_setting(name) ||
         is_decimal_token(setting);
}

auto run(const Config& cfg) -> int {
  if (cfg.all && cfg.save) {
    safeErrorPrintLn(
        "stty: the options for verbose and stty-readable output styles are");
    safeErrorPrintLn("mutually exclusive");
    return 1;
  }
  if ((cfg.all || cfg.save) && !cfg.settings.empty()) {
    safeErrorPrintLn(
        "stty: when specifying an output style, modes may not be set");
    return 1;
  }
  if (!validate_settings(cfg.settings)) {
    return 1;
  }

  HANDLE hCon;
  bool close_handle = false;

  if (!cfg.device.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(cfg.device, ec)) {
      return report_missing_device(cfg.device);
    }
    std::wstring wdevice = utf8_to_wstring(cfg.device);
    hCon = CreateFileW(wdevice.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hCon == INVALID_HANDLE_VALUE) {
      safeErrorPrint("stty: ");
      safeErrorPrint(cfg.device);
      safeErrorPrintLn(": ");
      safeErrorPrintLn(win32_posix_error_text(GetLastError()));
      return 1;
    }
    close_handle = true;
  } else {
    hCon = GetStdHandle(STD_INPUT_HANDLE);
  }

  if (!stdin_is_console(hCon)) {
    if (close_handle) CloseHandle(hCon);
    return report_inappropriate_ioctl(cfg);
  }

  if (!cfg.settings.empty()) {
    bool ok = true;
    for (size_t i = 0; i < cfg.settings.size(); ++i) {
      const auto& setting = cfg.settings[i];
      const auto name = normalized_setting_name(setting);
      if (is_value_setting(name)) {
        ++i;
        continue;
      }
      if (!apply_setting(hCon, setting)) {
        safeErrorPrint("stty: invalid argument " +
                       std::string(1, static_cast<char>(39)));
        safeErrorPrint(setting);
        safeErrorPrintLn(std::string(1, static_cast<char>(39)));
        print_try_help();
        ok = false;
      }
    }
    int result = ok ? 0 : 1;
    if (close_handle) CloseHandle(hCon);
    return result;
  }

  if (cfg.save) {
    print_machine_readable(hCon);
    if (close_handle) CloseHandle(hCon);
    return 0;
  }

  print_console_settings(hCon);
  if (close_handle) CloseHandle(hCon);
  return 0;
}

}  // namespace stty_pipeline

REGISTER_COMMAND(
    stty, "stty", "stty [-a|--all] [-g|--save] [SETTING...]",
    "Print or change terminal characteristics.\n"
    "\n"
    "Handle the tty line connected to standard input, or the device named\n"
    "by -F/--file.  With no arguments, prints all terminal settings in\n"
    "human-readable form.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all     print all current settings in human-readable form\n"
    "  -g, --save    print all current settings in a stty-readable form\n"
    "  -F, --file    open and use the specified device instead of stdin\n"
    "\n"
    "Optional - before SETTING indicates negation.  An * marks non-POSIX\n"
    "settings.  The underlying system defines which settings are available.\n"
    "\n"
    "Settings with effect on the Windows console:\n"
    "\n"
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
    "The settings below are accepted for GNU compatibility so that scripts\n"
    "do not break, but the Windows console keeps its own behavior: they are\n"
    "validated and silently ignored.\n"
    "\n"
    "Special characters:\n"
    " * discard CHAR  CHAR will toggle discarding of output\n"
    "   eof CHAR      CHAR will send an end of file (terminate the input)\n"
    "   eol CHAR      CHAR will end the line\n"
    " * eol2 CHAR     alternate CHAR for ending the line\n"
    "   erase CHAR    CHAR will erase the last character typed\n"
    "   intr CHAR     CHAR will send an interrupt signal\n"
    "   kill CHAR     CHAR will erase the current line\n"
    " * lnext CHAR    CHAR will enter the next character quoted\n"
    "   quit CHAR     CHAR will send a quit signal\n"
    " * rprnt CHAR    CHAR will redraw the current line\n"
    "   start CHAR    CHAR will restart the output after stopping it\n"
    "   stop CHAR     CHAR will stop the output\n"
    "   susp CHAR     CHAR will send a terminal stop signal\n"
    " * swtch CHAR    CHAR will switch to a different shell layer\n"
    " * werase CHAR   CHAR will erase the last word typed\n"
    "\n"
    "Special settings:\n"
    "   N             set the input and output speeds to N bauds\n"
    " * cols N        tell the kernel that the terminal has N columns\n"
    " * columns N     same as cols N\n"
    "   ispeed N      set the input speed to N\n"
    " * line N        use line discipline N\n"
    "   min N         with -icanon, set N characters minimum for a read\n"
    "   ospeed N      set the output speed to N\n"
    " * rows N        tell the kernel that the terminal has N rows\n"
    "   speed         print the terminal speed (accepted only)\n"
    "   time N        with -icanon, set read timeout in tenths of a second\n"
    "\n"
    "Control settings:\n"
    "   [-]clocal     disable modem control signals\n"
    "   [-]cread      allow input to be received\n"
    " * [-]crtscts    enable RTS/CTS handshaking\n"
    "   csN           set character size to N bits, N in [5..8]\n"
    "   [-]cstopb     use two stop bits per character (one with '-')\n"
    "   [-]hup        send a hangup signal when the last process closes\n"
    "   [-]hupcl      same as [-]hup\n"
    "   [-]parenb     generate parity bit in output and expect it in input\n"
    "   [-]parodd     set odd parity (or even parity with '-')\n"
    "\n"
    "Input settings:\n"
    "   [-]brkint     breaks cause an interrupt signal\n"
    "   [-]icrnl      translate carriage return to newline\n"
    "   [-]ignbrk     ignore break characters\n"
    "   [-]igncr      ignore carriage return\n"
    "   [-]ignpar     ignore characters with parity errors\n"
    " * [-]imaxbel    beep and do not flush a full input buffer\n"
    "   [-]inlcr      translate newline to carriage return\n"
    "   [-]inpck      enable input parity checking\n"
    "   [-]istrip     clear high (8th) bit of input characters\n"
    " * [-]iutf8      assume input characters are UTF-8 encoded\n"
    " * [-]iuclc      translate uppercase characters to lowercase\n"
    " * [-]ixany      let any character restart output\n"
    "   [-]ixoff      enable sending of start/stop characters\n"
    "   [-]ixon       enable XON/XOFF flow control\n"
    "   [-]parmrk     mark parity errors (with a 255-0-character sequence)\n"
    "   [-]tandem     same as [-]ixoff\n"
    "\n"
    "Output settings:\n"
    " * bsN           backspace delay style, N in [0..1]\n"
    " * crN           carriage return delay style, N in [0..3]\n"
    " * ffN           form feed delay style, N in [0..1]\n"
    " * nlN           newline delay style, N in [0..1]\n"
    " * [-]ocrnl      translate carriage return to newline\n"
    " * [-]ofdel      use delete characters for fill instead of NULs\n"
    " * [-]ofill      use fill characters instead of timing for delays\n"
    " * [-]olcuc      translate lowercase characters to uppercase\n"
    " * [-]onlcr      translate newline to carriage return-newline\n"
    " * [-]onlret     newline performs a carriage return\n"
    " * [-]onocr      do not print carriage returns in the first column\n"
    "   [-]opost      postprocess output\n"
    " * tabN          horizontal tab delay style, N in [0..3]\n"
    " * tabs          same as tab0\n"
    " * -tabs         same as tab3\n"
    " * vtN           vertical tab delay style, N in [0..1]\n"
    "\n"
    "Local settings:\n"
    "   [-]crterase   echo erase characters as backspace-space-backspace\n"
    " * [-]ctlecho    echo control characters in hat notation ('^c')\n"
    " * [-]echoctl    same as [-]ctlecho\n"
    "   [-]echoe      same as [-]crterase\n"
    "   [-]echok      echo a newline after a kill character\n"
    " * [-]echoke     same as [-]crtkill\n"
    "   [-]echonl     echo newline even if not echoing other characters\n"
    " * [-]echoprt    echo erased characters backward, between \\ and /\n"
    " * [-]extproc    enable \"LINEMODE\"; useful with high latency links\n"
    " * [-]flusho     discard output\n"
    "   [-]iexten     enable non-POSIX special characters\n"
    "   [-]noflsh     disable flushing after interrupt and quit\n"
    " * [-]prterase   same as [-]echoprt\n"
    " * [-]tostop     stop background jobs that try to write the terminal\n"
    " * [-]xcase      with icanon, escape with \\ for uppercase characters\n"
    "\n"
    "Combination settings (accepted only):\n"
    "\n"
    "   ek            erase and kill characters to their default values\n"
    "   evenp         same as parenb -parodd cs7\n"
    "   -evenp        same as -parenb cs8\n"
    "   [-]parity     same as [-]evenp\n"
    "   oddp          same as parenb parodd cs7\n"
    "   -oddp         same as -parenb cs8\n"
    "   nl            same as -icrnl -onlcr\n"
    "   -nl           same as icrnl -inlcr -igncr onlcr -ocrnl -onlret\n"
    "   pass8         same as -parenb -istrip cs8\n"
    "   -pass8        same as parenb istrip cs7\n"
    "   litout        same as -parenb -istrip -opost cs8\n"
    "   -litout       same as parenb istrip opost cs7\n"
    " * [-]decctlq    same as [-]ixany\n"
    " * tabs          same as tab0\n"
    " * [-]lcase      same as xcase iuclc olcuc\n"
    " * [-]LCASE      same as [-]lcase\n"
    "   crt           same as echoe echoctl echoke\n"
    " * crtkill       kill all line by obeying the echoprt and echoe\n"
    "                 settings\n"
    "   dec           same as echoe echoctl echoke -ixany intr ^c erase\n"
    "                 0177 kill ^u\n"
    "\n"
    "Note: On Windows, only the settings listed as effective above (sane,\n"
    "raw, cooked, cbreak, echo, icanon and isig) change console behavior;\n"
    "every other setting is accepted but silently ignored.",
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
