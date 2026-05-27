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
 *  - File: dircolors.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dircolors.
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

auto constexpr DIRCOLORS_OPTIONS = std::array{
    OPTION("-b", "--sh", "output Bourne shell code to set LS_COLORS"),
    OPTION("-c", "--csh", "output C shell code to set LS_COLORS"),
    OPTION("-p", "--print-database", "output defaults"),
    OPTION("", "--print-ls-colors", "output fully escaped colors for display")};

namespace dircolors_pipeline {
namespace cp = core::pipeline;

// Default LS_COLORS database
const std::vector<std::pair<std::string, std::string>> DEFAULT_COLORS = {
    {"no", "0"},    {"fi", "0"},      {"rs", "0"},   {"di", "01;34"},
    {"ln", "01;36"}, {"mh", "00"},    {"pi", "40;33"}, {"so", "01;35"},
    {"do", "01;35"}, {"bd", "40;33;01"}, {"cd", "40;33;01"}, {"or", "40;31;01"},
    {"mi", "00"},    {"su", "37;41"},  {"sg", "30;43"}, {"ca", "30;41"},
    {"tw", "30;42"}, {"ow", "34;42"},  {"st", "37;44"}, {"ex", "01;32"},
    // Archives
    {"tar", "01;31"}, {"tgz", "01;31"}, {"arc", "01;31"}, {"arj", "01;31"},
    {"taz", "01;31"}, {"lha", "01;31"}, {"lz4", "01;31"}, {"lzh", "01;31"},
    {"lzma", "01;31"}, {"tlz", "01;31"}, {"txz", "01;31"}, {"tzo", "01;31"},
    {"t7z", "01;31"}, {"zip", "01;31"}, {"z", "01;31"},   {"dz", "01;31"},
    {"gz", "01;31"},  {"lrz", "01;31"}, {"lz", "01;31"},  {"lzo", "01;31"},
    {"xz", "01;31"},  {"zst", "01;31"}, {"tzst", "01;31"}, {"bz2", "01;31"},
    {"bz", "01;31"},  {"tbz", "01;31"}, {"tbz2", "01;31"}, {"tz", "01;31"},
    {"deb", "01;31"}, {"rpm", "01;31"}, {"jar", "01;31"}, {"war", "01;31"},
    {"ear", "01;31"}, {"sar", "01;31"}, {"rar", "01;31"}, {"alz", "01;31"},
    {"cab", "01;31"}, {"wim", "01;31"}, {"swm", "01;31"}, {"dwm", "01;31"},
    {"esd", "01;31"},
    // Image formats
    {"jpg", "01;35"}, {"jpeg", "01;35"}, {"mjpg", "01;35"}, {"mjpeg", "01;35"},
    {"gif", "01;35"}, {"bmp", "01;35"}, {"pbm", "01;35"}, {"pgm", "01;35"},
    {"ppm", "01;35"}, {"tga", "01;35"}, {"xbm", "01;35"}, {"xpm", "01;35"},
    {"tif", "01;35"}, {"tiff", "01;35"}, {"png", "01;35"}, {"svg", "01;35"},
    {"svgz", "01;35"}, {"mng", "01;35"}, {"pcx", "01;35"}, {"mov", "01;35"},
    {"mpg", "01;35"}, {"mpeg", "01;35"}, {"m2v", "01;35"}, {"mkv", "01;35"},
    {"webm", "01;35"}, {"webp", "01;35"}, {"ogm", "01;35"}, {"mp4", "01;35"},
    {"m4v", "01;35"}, {"mp4v", "01;35"}, {"vob", "01;35"}, {"qt", "01;35"},
    {"nuv", "01;35"}, {"wmv", "01;35"}, {"asf", "01;35"}, {"rm", "01;35"},
    {"rmvb", "01;35"}, {"flc", "01;35"}, {"avi", "01;35"}, {"fli", "01;35"},
    {"flv", "01;35"}, {"gl", "01;35"},  {"dl", "01;35"},  {"xcf", "01;35"},
    {"xwd", "01;35"}, {"yuv", "01;35"}, {"cgm", "01;35"}, {"emf", "01;35"},
    {"ogv", "01;35"}, {"ogx", "01;35"},
    // Audio formats
    {"aac", "00;36"}, {"au", "00;36"},  {"flac", "00;36"}, {"m4a", "00;36"},
    {"mid", "00;36"}, {"midi", "00;36"}, {"mka", "00;36"}, {"mp3", "00;36"},
    {"mpc", "00;36"}, {"ogg", "00;36"}, {"ra", "00;36"},  {"wav", "00;36"},
    {"oga", "00;36"}, {"opus", "00;36"}, {"spx", "00;36"}, {"xspf", "00;36"}};

auto print_bourne_shell() -> void {
  safePrint("LS_COLORS='");
  for (size_t i = 0; i < DEFAULT_COLORS.size(); ++i) {
    if (i > 0) safePrint(":");
    safePrint(DEFAULT_COLORS[i].first + "=" + DEFAULT_COLORS[i].second);
  }
  safePrintLn("';");
  safePrintLn("export LS_COLORS");
}

auto print_csh_shell() -> void {
  safePrint("setenv LS_COLORS '");
  for (size_t i = 0; i < DEFAULT_COLORS.size(); ++i) {
    if (i > 0) safePrint(":");
    safePrint(DEFAULT_COLORS[i].first + "=" + DEFAULT_COLORS[i].second);
  }
  safePrintLn("';");
}

auto print_database() -> void {
  safePrintLn("# Configuration file for dircolors, a utility to help you set the");
  safePrintLn("# LS_COLORS environment variable used by GNU ls.");
  safePrintLn("");
  for (const auto& [key, value] : DEFAULT_COLORS) {
    safePrintLn(key + " " + value);
  }
}

auto print_ls_colors() -> void {
  for (size_t i = 0; i < DEFAULT_COLORS.size(); ++i) {
    if (i > 0) safePrint(":");
    safePrint(DEFAULT_COLORS[i].first + "=" + DEFAULT_COLORS[i].second);
  }
  safePrintLn("");
}

auto run(const CommandContext<DIRCOLORS_OPTIONS.size()>& ctx) -> int {
  bool bourne = ctx.get<bool>("-b", false) || ctx.get<bool>("--sh", false);
  bool csh = ctx.get<bool>("-c", false) || ctx.get<bool>("--csh", false);
  bool print_db =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--print-database", false);
  bool print_colors = ctx.get<bool>("--print-ls-colors", false);

  if (print_db) {
    print_database();
    return 0;
  }

  if (print_colors) {
    print_ls_colors();
    return 0;
  }

  if (csh) {
    print_csh_shell();
    return 0;
  }

  // Default to Bourne shell
  print_bourne_shell();
  return 0;
}

}  // namespace dircolors_pipeline

REGISTER_COMMAND(
    dircolors, "dircolors", "dircolors [OPTION]... [FILE]",
    "Output commands to set the LS_COLORS environment variable.\n"
    "\n"
    "Determine format of output:\n"
    "  -b, --sh           output Bourne shell code to set LS_COLORS (default)\n"
    "  -c, --csh          output C shell code to set LS_COLORS\n"
    "  -p, --print-database  output defaults\n"
    "      --print-ls-colors  output fully escaped colors for display\n"
    "\n"
    "If FILE is specified, read it to determine which colors to use for which\n"
    "file types and extensions.  Otherwise, a precompiled database is used.\n"
    "\n"
    "Note: On Windows, dircolors outputs color codes compatible with ls --color.",
    "  dircolors         output Bourne shell code (default)\n"
    "  dircolors -c      output C shell code\n"
    "  dircolors -p      print default color database\n"
    "  eval \"$(dircolors)\"  set LS_COLORS in current shell",
    "ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", DIRCOLORS_OPTIONS) {
  using namespace dircolors_pipeline;
  using namespace core::pipeline;

  return run(ctx);
}
