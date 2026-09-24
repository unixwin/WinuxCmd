/*
 *  Copyright © 2026 WinuxCmd
 */
#include <time.h>

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr TZSET_OPTIONS = std::array{
    // [EXT] option
    OPTION("", "--help", "output usage information and exit", BOOL_TYPE),
    // [EXT] option
    OPTION("-V", "--version", "output version information and exit",
           BOOL_TYPE)};

namespace {

void print_usage_stdout() {
  constexpr std::string_view help =
      "Usage: tzset [OPTION]\n\n"
      "Print POSIX-compatible timezone ID from current Windows timezone "
      "setting\n\n"
      "Options:\n"
      "      --help               output usage information and exit.\n"
      "  -V, --version            output version information and exit.\n\n"
      "Use tzset to set your TZ variable. In POSIX-compatible shells like "
      "bash,\n"
      "dash, mksh, or zsh:\n\n"
      "      export TZ=$(tzset)\n\n"
      "In csh-compatible shells like tcsh:\n\n"
      "      setenv TZ `tzset`\n";
  safePrint(cmd::meta::format_custom_help(
      "tzset", winux::i18n::translate("command.tzset.custom_help", help)));
}

void print_usage_stderr() {
  safeErrorPrintLn("Usage: tzset [OPTION]");
  safeErrorPrintLn("");
  safeErrorPrintLn(
      "Print POSIX-compatible timezone ID from current Windows timezone "
      "setting");
}

auto is_known_posix_timezone(std::string_view value) -> bool {
  static constexpr std::array<std::string_view, 11> zones = {
      "Asia/Shanghai",       "UTC",
      "Europe/London",       "Europe/Berlin",
      "Europe/Budapest",     "America/New_York",
      "America/Chicago",     "America/Denver",
      "America/Los_Angeles", "Asia/Tokyo",
      "Asia/Seoul"};
  return std::ranges::find(zones, value) != zones.end();
}

auto key_to_posix(std::wstring_view key) -> std::string {
  static const std::vector<std::pair<std::wstring_view, std::string_view>> map =
      {{L"China Standard Time", "Asia/Shanghai"},
       {L"UTC", "UTC"},
       {L"GMT Standard Time", "Europe/London"},
       {L"W. Europe Standard Time", "Europe/Berlin"},
       {L"Central Europe Standard Time", "Europe/Budapest"},
       {L"Eastern Standard Time", "America/New_York"},
       {L"Central Standard Time", "America/Chicago"},
       {L"Mountain Standard Time", "America/Denver"},
       {L"Pacific Standard Time", "America/Los_Angeles"},
       {L"Tokyo Standard Time", "Asia/Tokyo"},
       {L"Korea Standard Time", "Asia/Seoul"}};
  for (const auto& [win_key, posix] : map) {
    if (_wcsicmp(key.data(), win_key.data()) == 0) {
      return std::string(posix);
    }
  }
  return {};
}

auto fallback_posix_id(const DYNAMIC_TIME_ZONE_INFORMATION& tz) -> std::string {
  LONG bias = tz.Bias + tz.StandardBias;
  const int hours = static_cast<int>(std::abs(bias) / 60);
  std::string name = wstring_to_utf8(tz.StandardName);
  std::string abbr;
  for (unsigned char ch : name) {
    if (std::isalpha(ch)) {
      abbr.push_back(static_cast<char>(std::toupper(ch)));
      if (abbr.size() == 3) break;
    }
  }
  if (abbr.empty()) abbr = "TZ";
  return abbr + (bias <= 0 ? "-" : "+") + std::to_string(hours);
}

auto current_posix_timezone() -> std::optional<std::string> {
  DYNAMIC_TIME_ZONE_INFORMATION tz{};
  if (GetDynamicTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID) {
    return std::nullopt;
  }
  if (auto mapped = key_to_posix(tz.TimeZoneKeyName); !mapped.empty()) {
    return mapped;
  }
  return fallback_posix_id(tz);
}

}  // namespace

REGISTER_COMMAND(
    tzset, "tzset", "tzset [OPTION]",
    "Print POSIX-compatible timezone ID from current Windows timezone setting",
    "  tzset\n  export TZ=$(tzset)", "date", "WinuxCmd",
    "Copyright © 2026 WinuxCmd", TZSET_OPTIONS) {
  if (ctx.get<bool>("--help", false)) {
    print_usage_stdout();
    return 0;
  }
  if (!ctx.positionals.empty()) {
    std::string requested(ctx.positionals[0]);
    if (!is_known_posix_timezone(requested)) {
      print_usage_stderr();
      return 1;
    }
    safePrintLn("TZ=" + requested);
    return 0;
  }
  auto tz = current_posix_timezone();
  if (!tz) return 1;
  safePrintLn("TZ=" + *tz);
  return 0;
}
