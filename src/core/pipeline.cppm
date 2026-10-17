// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:pipeline;

import std;
import utils;

export namespace core::pipeline {
using Error = std::string;

template <typename T>
using Result = std::expected<T, Error>;

template <typename T>
void report_error(const Result<T>& result, std::wstring_view command_name) {
  if (!result) {
    const auto& error_msg = result.error();
    safeErrorPrintLn(std::wstring(command_name) + L": " +
                     utf8_to_wstring(winux::i18n::translate_error(error_msg)));
  }
}

template <typename T>
int report_error_with_code(const Result<T>& result,
                           std::wstring_view command_name, int error_code = 1) {
  if (!result) {
    report_error(result, command_name);
    return error_code;
  }
  return 0;
}

inline void report_custom_error(std::wstring_view command_name,
                                std::wstring_view error_message) {
  const auto translated =
      winux::i18n::translate_error(wstring_to_utf8(error_message));
  safeErrorPrintLn(std::wstring(command_name) + L": " +
                   utf8_to_wstring(translated));
}

// [GNU] coreutils commands that take no operands report a stray operand as
// "<cmd>: extra operand 'X'" followed by the usual usage hint and exit 1
// (the `if (optind < argc) { error (...); usage (EXIT_FAILURE); }` idiom).
// Shared so uname/arch/whoami/logname/hostid all emit the identical shape.
inline int report_extra_operand(std::string_view command_name,
                                std::string_view operand) {
  const std::string raw = "extra operand '" + std::string(operand) + "'";
  safeErrorPrintLn(std::string(command_name) + ": " +
                   winux::i18n::translate_error(raw));
  safeErrorPrintLn(winux::i18n::format("common.try_help",
                                       "Try '{} --help' for more information.",
                                       command_name));
  return 1;
}

}  // namespace core::pipeline
