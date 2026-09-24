// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:dispatcher;

import std;
import :cmd_meta;
import :command_context;
import utils;
import version;

export template <size_t N>
using CommandFunc = int (*)(CommandContext<N> &) noexcept;

export using CommandInvoker = int (*)(std::span<std::string_view>) noexcept;

export template <size_t N>
struct CommandEntry {
  cmd::meta::CommandMetaHandle meta;
  CommandFunc<N> handler;
  std::string_view brief_desc;

  CommandEntry() = default;
  CommandEntry(cmd::meta::CommandMetaHandle m, CommandFunc<N> h)
      : meta(std::move(m)), handler(h), brief_desc(meta.brief_desc()) {}
};

// Per-option information returned for completion
export struct OptionInfo {
  std::string short_name;
  std::string long_name;
  std::string description;
};

struct CommandEntryErased {
  std::span<const cmd::meta::OptionMeta> options;
  CommandInvoker handler = nullptr;
  std::string_view brief_desc;

  CommandEntryErased() = default;

  CommandEntryErased(std::span<const cmd::meta::OptionMeta> opts,
                     CommandInvoker h, std::string_view brief)
      : options(opts), handler(h), brief_desc(brief) {}
};

using ArgsRewriteHook =
    std::optional<std::vector<std::string>> (*)(std::span<std::string_view>);
using ArgsValidationHook =
    std::optional<std::string> (*)(std::span<std::string_view>);
using SpecialDispatchHook = std::optional<int> (*)(const CommandEntryErased &,
                                                   std::span<std::string_view>);
using StandardInterceptionHook = bool (*)(std::span<std::string_view>);

constexpr size_t kMaxRewriteHooks = 5;

struct CommandBehavior {
  int parse_error_exit_code = 1;
  ArgsValidationHook validate_args = nullptr;
  std::string_view validation_prefix;
  std::array<ArgsRewriteHook, kMaxRewriteHooks> rewrite_hooks{};
  size_t rewrite_hook_count = 0;
  SpecialDispatchHook special_dispatch = nullptr;
  StandardInterceptionHook standard_interception_enabled = nullptr;
  // [GNU] printf, test and [ never call getopt: they recognize --help and
  // --version only as the sole command-line argument (printf.c/test.c use
  // `argc == 2`); anywhere else the token is an operand or format string.
  bool help_version_only_when_sole_argument = false;
};

auto is_posixly_correct() -> bool {
  const char *value = std::getenv("POSIXLY_CORRECT");
  return value != nullptr && value[0] != '\0';
}

auto legacy_count_value(std::string_view arg) -> std::string {
  if (arg.empty()) return {};
  if ((arg[0] == '-' || arg[0] == '+') && arg.size() > 1) {
    return std::string(arg.substr(1));
  }
  return std::string(arg);
}

auto append_remaining_args(std::vector<std::string> &out,
                           std::span<std::string_view> args, size_t start)
    -> void {
  for (size_t i = start; i < args.size(); ++i) {
    out.emplace_back(args[i]);
  }
}

auto parse_decimal_prefix(std::string_view arg, size_t start) -> size_t {
  size_t pos = start;
  while (pos < arg.size() &&
         std::isdigit(static_cast<unsigned char>(arg[pos])) != 0) {
    ++pos;
  }
  return pos;
}

auto rewrite_head_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) return std::nullopt;
  std::string_view first = args[0];
  if (first.size() < 2 || first[0] != '-' || first[1] == '-')
    return std::nullopt;

  size_t suffix_pos = parse_decimal_prefix(first, 1);
  if (suffix_pos == 1) return std::nullopt;

  std::string count(first.substr(1, suffix_pos - 1));
  std::string mode = "-n";
  std::string value = count;
  std::vector<std::string> flags;

  for (size_t i = suffix_pos; i < first.size(); ++i) {
    switch (first[i]) {
      case 'l':
        mode = "-n";
        value = count;
        break;
      case 'c':
        mode = "-c";
        value = count;
        break;
      case 'b':
        mode = "-c";
        value = count + "b";
        break;
      case 'k':
        mode = "-c";
        value = count + "K";
        break;
      case 'm':
        mode = "-c";
        value = count + "M";
        break;
      case 'q':
        flags.emplace_back("-q");
        break;
      case 'v':
        flags.emplace_back("-v");
        break;
      default:
        return std::nullopt;
    }
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + flags.size() + 1);
  rewritten.emplace_back(mode);
  rewritten.emplace_back(value);
  for (const auto &flag : flags) rewritten.emplace_back(flag);
  append_remaining_args(rewritten, args, 1);
  return rewritten;
}

auto rewrite_tail_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) return std::nullopt;
  std::string_view first = args[0];

  if (first.size() >= 2 && first[0] == '+' && first[1] != '+') {
    size_t suffix_pos = parse_decimal_prefix(first, 1);
    if (suffix_pos == 1) return std::nullopt;

    std::string count(first.substr(1, suffix_pos - 1));
    std::string mode = "-n";
    std::string value = "+" + count;

    for (size_t i = suffix_pos; i < first.size(); ++i) {
      switch (first[i]) {
        case 'l':
          mode = "-n";
          value = "+" + count;
          break;
        case 'c':
          mode = "-c";
          value = "+" + count;
          break;
        case 'b':
          mode = "-c";
          value = "+" + count + "b";
          break;
        default:
          return std::nullopt;
      }
    }

    std::vector<std::string> rewritten;
    rewritten.reserve(args.size() + 1);
    rewritten.emplace_back(mode);
    rewritten.emplace_back(value);
    append_remaining_args(rewritten, args, 1);
    return rewritten;
  }

  if (first.size() < 2 || first[0] != '-' || first[1] == '-')
    return std::nullopt;

  size_t suffix_pos = parse_decimal_prefix(first, 1);
  if (suffix_pos == 1) return std::nullopt;

  bool has_compact_suffix = suffix_pos < first.size();
  if (has_compact_suffix && args.size() > 2) return std::nullopt;

  std::string count(first.substr(1, suffix_pos - 1));
  std::string mode = "-n";
  std::string value = count;
  bool follow = false;

  for (size_t i = suffix_pos; i < first.size(); ++i) {
    switch (first[i]) {
      case 'l':
        mode = "-n";
        value = count;
        break;
      case 'c':
        mode = "-c";
        value = count;
        break;
      case 'b':
        mode = "-c";
        value = count + "b";
        break;
      case 'f':
        follow = true;
        break;
      default:
        return std::nullopt;
    }
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 2);
  rewritten.emplace_back(mode);
  rewritten.emplace_back(value);
  if (follow) rewritten.emplace_back("-f");
  append_remaining_args(rewritten, args, 1);
  return rewritten;
}

auto is_head_obsolete_count_arg(std::string_view arg) -> bool {
  if (arg.size() < 2 || arg[0] != '-' || arg[1] == '-') return false;

  size_t suffix_pos = parse_decimal_prefix(arg, 1);
  if (suffix_pos == 1) return false;

  for (size_t i = suffix_pos; i < arg.size(); ++i) {
    switch (arg[i]) {
      case 'l':
      case 'c':
      case 'b':
      case 'k':
      case 'm':
      case 'q':
      case 'v':
        break;
      default:
        return false;
    }
  }

  return true;
}

auto is_tail_obsolete_count_arg(std::string_view arg) -> bool {
  if (arg.size() < 2) return false;

  if (arg[0] == '+') {
    if (arg[1] == '+') return false;

    size_t suffix_pos = parse_decimal_prefix(arg, 1);
    if (suffix_pos == 1) return false;
    for (size_t i = suffix_pos; i < arg.size(); ++i) {
      if (arg[i] != 'l' && arg[i] != 'c' && arg[i] != 'b') return false;
    }
    return true;
  }

  if (arg[0] != '-' || arg[1] == '-') return false;
  size_t suffix_pos = parse_decimal_prefix(arg, 1);
  if (suffix_pos == 1) return false;
  for (size_t i = suffix_pos; i < arg.size(); ++i) {
    if (arg[i] != 'l' && arg[i] != 'c' && arg[i] != 'b' && arg[i] != 'f') {
      return false;
    }
  }
  return true;
}

auto tail_invalid_obsolete_count_context(std::span<std::string_view> args)
    -> std::optional<std::string> {
  if (args.size() < 2) return std::nullopt;

  bool stop_option_parsing = false;
  bool expect_value = false;

  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view arg = args[i];

    if (stop_option_parsing) continue;
    if (arg == "--") {
      stop_option_parsing = true;
      continue;
    }
    if (expect_value) {
      expect_value = false;
      continue;
    }

    if (i > 0 && is_tail_obsolete_count_arg(arg)) {
      return legacy_count_value(arg);
    }

    if (arg == "-c" || arg == "--bytes" || arg == "-n" || arg == "--lines" ||
        arg == "-s" || arg == "--sleep-interval" ||
        arg == "--max-unchanged-stats" || arg == "--pid") {
      expect_value = true;
      continue;
    }

    if (arg == "--follow" && i + 1 < args.size() && args[i + 1] != "--" &&
        !args[i + 1].empty() && args[i + 1][0] != '-') {
      expect_value = true;
    }
  }

  return std::nullopt;
}

auto head_invalid_obsolete_count_context(std::span<std::string_view> args)
    -> std::optional<std::string> {
  if (args.size() < 2) return std::nullopt;

  bool stop_option_parsing = false;
  bool expect_value = false;

  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view arg = args[i];

    if (stop_option_parsing) continue;
    if (arg == "--") {
      stop_option_parsing = true;
      continue;
    }
    if (expect_value) {
      expect_value = false;
      continue;
    }

    if (i > 0 && is_head_obsolete_count_arg(arg)) {
      return legacy_count_value(arg);
    }

    if (arg == "-c" || arg == "--bytes" || arg == "-n" || arg == "--lines") {
      expect_value = true;
    }
  }

  return std::nullopt;
}

auto is_chmod_negative_symbolic_mode_arg(std::string_view arg) -> bool {
  if (arg.size() < 2 || arg[0] != '-' || arg[1] == '-') {
    return false;
  }

  return std::ranges::all_of(arg.substr(1), [](unsigned char ch) {
    return ch == 'r' || ch == 'w' || ch == 'x';
  });
}

auto rewrite_chmod_gnu_negative_mode_args(std::string_view cmdName,
                                          std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName != "chmod" || args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> negative_modes;
  std::vector<std::string> pre_double_hyphen_args;
  std::vector<std::string> post_double_hyphen_args;
  bool seen_double_hyphen = false;

  for (auto arg : args) {
    if (!seen_double_hyphen && arg == "--") {
      seen_double_hyphen = true;
      continue;
    }

    if (!seen_double_hyphen && is_chmod_negative_symbolic_mode_arg(arg)) {
      negative_modes.push_back("a" + std::string(arg));
      continue;
    }

    if (seen_double_hyphen) {
      post_double_hyphen_args.emplace_back(arg);
    } else {
      pre_double_hyphen_args.emplace_back(arg);
    }
  }

  if (negative_modes.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);

  std::string merged_mode;
  for (size_t i = 0; i < negative_modes.size(); ++i) {
    if (i != 0) {
      merged_mode.push_back(',');
    }
    merged_mode += negative_modes[i];
  }

  rewritten.push_back(std::move(merged_mode));
  for (const auto &arg : pre_double_hyphen_args) {
    rewritten.push_back(arg);
  }
  if (seen_double_hyphen) {
    rewritten.push_back("--");
    for (const auto &arg : post_double_hyphen_args) {
      rewritten.push_back(arg);
    }
  }

  return rewritten;
}

auto rewrite_chmod_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  return rewrite_chmod_gnu_negative_mode_args("chmod", args);
}

auto is_chattr_mode_arg(std::string_view arg) -> bool {
  if (arg.size() < 2) return false;
  if (arg[0] != '+' && arg[0] != '-' && arg[0] != '=') return false;
  if (arg == "-R" || arg == "-V") return false;
  if (arg.starts_with("--")) return false;

  return std::ranges::all_of(arg.substr(1), [](unsigned char ch) {
    switch (ch) {
      case 'R':
      case 'r':
      case 'H':
      case 'h':
      case 'S':
      case 's':
      case 'A':
      case 'a':
      case 'I':
      case 'i':
      case 'T':
      case 't':
      case 'O':
      case 'o':
        return true;
      default:
        return false;
    }
  });
}

auto rewrite_chattr_mode_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  bool seen_double_hyphen = false;
  for (size_t i = 0; i < args.size(); ++i) {
    auto arg = args[i];
    if (arg == "--") {
      seen_double_hyphen = true;
      continue;
    }
    if (seen_double_hyphen || !is_chattr_mode_arg(arg)) continue;

    std::vector<std::string> rewritten;
    rewritten.reserve(args.size() + 1);
    for (size_t j = 0; j < i; ++j) rewritten.emplace_back(args[j]);
    rewritten.emplace_back("--");
    append_remaining_args(rewritten, args, i);
    return rewritten;
  }
  return std::nullopt;
}

auto rewrite_chattr_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  return rewrite_chattr_mode_args(args);
}

auto parse_legacy_nice_adjustment(std::string_view arg) -> std::optional<int> {
  if (arg.size() < 2 || arg[0] != '-') {
    return std::nullopt;
  }

  if (arg == "-n" || arg == "--adjustment") {
    return std::nullopt;
  }

  std::string_view numeric = arg.substr(1);
  if (numeric.empty() || numeric == "-" || numeric == "+") {
    return std::nullopt;
  }

  if (numeric[0] == '+') {
    numeric.remove_prefix(1);
    if (numeric.empty()) {
      return std::nullopt;
    }
  }

  int value = 0;
  auto [ptr, ec] =
      std::from_chars(numeric.data(), numeric.data() + numeric.size(), value);
  if (ec != std::errc() || ptr != numeric.data() + numeric.size()) {
    return std::nullopt;
  }

  return value;
}

auto rewrite_nice_legacy_args(std::string_view cmdName,
                              std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName != "nice" || args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size());
  bool changed = false;
  bool seen_command = false;
  bool expecting_adjustment_value = false;

  for (auto arg : args) {
    if (seen_command) {
      rewritten.emplace_back(arg);
      continue;
    }

    if (arg == "--") {
      rewritten.emplace_back(arg);
      seen_command = true;
      expecting_adjustment_value = false;
      continue;
    }

    if (expecting_adjustment_value) {
      rewritten.emplace_back(arg);
      expecting_adjustment_value = false;
      continue;
    }

    if (arg == "-n" || arg == "--adjustment") {
      rewritten.emplace_back(arg);
      expecting_adjustment_value = true;
      continue;
    }

    if (arg.starts_with("-n") || arg.starts_with("--adjustment=")) {
      rewritten.emplace_back(arg);
      continue;
    }

    if (auto legacy_value = parse_legacy_nice_adjustment(arg)) {
      rewritten.emplace_back("-n" + std::to_string(*legacy_value));
      changed = true;
      continue;
    }

    if (!arg.empty() && arg[0] != '-') {
      rewritten.emplace_back("--");
      rewritten.emplace_back(arg);
      seen_command = true;
      changed = true;
      continue;
    }

    rewritten.emplace_back(arg);
  }

  if (!changed) {
    return std::nullopt;
  }

  return rewritten;
}

auto rewrite_nice_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  return rewrite_nice_legacy_args("nice", args);
}

auto rewrite_pr_legacy_column_args(std::string_view cmdName,
                                   std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (cmdName != "pr" || args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 2);
  bool changed = false;
  bool stop_option_parsing = false;

  for (auto arg : args) {
    if (!stop_option_parsing && arg == "--") {
      stop_option_parsing = true;
      rewritten.emplace_back(arg);
      continue;
    }

    if (!stop_option_parsing && arg.size() >= 2 && arg[0] == '-' &&
        arg[1] != '-') {
      size_t suffix_pos = parse_decimal_prefix(arg, 1);
      if (suffix_pos == arg.size() && suffix_pos > 1) {
        rewritten.emplace_back("-COLUMN");
        rewritten.emplace_back(std::string(arg.substr(1)));
        changed = true;
        continue;
      }
    }

    rewritten.emplace_back(arg);
  }

  if (!changed) {
    return std::nullopt;
  }

  return rewritten;
}

auto rewrite_pr_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  return rewrite_pr_legacy_column_args("pr", args);
}

// GNU fold accepts the obsolete "-WIDTH" form (e.g. "fold -5"). The rewrite
// happens in place so a later "-WIDTH" overrides an earlier "-w WIDTH", the
// same last-one-wins rule GNU gets from processing options in sequence.
// An argument is only eligible when it is not the value of a preceding
// "-w"/"--width" and does not appear after a "--" terminator.
auto rewrite_fold_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size());
  bool changed = false;
  bool stop_option_parsing = false;
  bool expect_width_value = false;

  for (auto arg : args) {
    if (!stop_option_parsing && arg == "--") {
      stop_option_parsing = true;
      rewritten.emplace_back(arg);
      expect_width_value = false;
      continue;
    }

    if (!stop_option_parsing && expect_width_value) {
      // This token is the value of -w/--width; leave it alone.
      rewritten.emplace_back(arg);
      expect_width_value = false;
      continue;
    }

    if (!stop_option_parsing && arg.size() >= 2 && arg[0] == '-' &&
        arg[1] != '-') {
      size_t suffix_pos = parse_decimal_prefix(arg, 1);
      if (suffix_pos == arg.size()) {
        rewritten.emplace_back("--width=" + std::string(arg.substr(1)));
        changed = true;
        continue;
      }
    }

    if (!stop_option_parsing && (arg == "-w" || arg == "--width")) {
      expect_width_value = true;
    }

    rewritten.emplace_back(arg);
  }

  if (!changed) {
    return std::nullopt;
  }
  return rewritten;
}

// GNU fmt accepts the obsolete "-WIDTH" form (e.g. "fmt -60"), but only when
// it is the very first argument; a digit option in any other position is a
// getopt error in fmt.c. The whole rest of the argument must be digits —
// "fmt -60s" fails with "invalid width: '60s'" — so the token is rewritten
// to "-w <rest>" and fmt's own validation reports the same diagnostic.
auto rewrite_fmt_obsolete_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (args.empty()) {
    return std::nullopt;
  }

  std::string_view first = args[0];
  if (first.size() < 2 || first[0] != '-' || first[1] == '-' ||
      !std::isdigit(static_cast<unsigned char>(first[1]))) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);
  rewritten.emplace_back("-w");
  rewritten.emplace_back(first.substr(1));
  append_remaining_args(rewritten, args, 1);
  return rewritten;
}

auto echo_posixly_correct_literal_mode(std::string_view cmdName,
                                       std::span<std::string_view> args)
    -> bool {
  if (cmdName != "echo") {
    return false;
  }

  const char *value = std::getenv("POSIXLY_CORRECT");
  if (value == nullptr || value[0] == '\0') {
    return false;
  }

  return args.empty() || args[0] != "-n";
}

auto command_declares_option(std::span<const cmd::meta::OptionMeta> options,
                             std::string_view name) -> bool {
  return std::ranges::any_of(options, [name](const auto &option) {
    return option.short_name == name || option.long_name == name;
  });
}
auto command_declares_version_short(
    std::span<const cmd::meta::OptionMeta> options, std::string_view name)
    -> bool {
  return std::ranges::any_of(options, [name](const auto &option) {
    return option.short_name == name && option.long_name == "--version";
  });
}
auto wants_standard_version(std::string_view cmdName,
                            std::span<std::string_view> args,
                            std::span<const cmd::meta::OptionMeta> options)
    -> bool {
  if (echo_posixly_correct_literal_mode(cmdName, args)) {
    return false;
  }
  for (const auto &arg : args) {
    if (arg == "--") {
      break;
    }
    if (arg == "--version") {
      return true;
    }
    if (arg == "-v" && command_declares_version_short(options, "-v")) {
      return true;
    }
    if (arg == "-V") {
      return !command_declares_option(options, arg) ||
             command_declares_version_short(options, arg);
    }
  }
  return false;
}
auto rewrite_echo_posix_args(std::string_view cmdName,
                             std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (!echo_posixly_correct_literal_mode(cmdName, args)) {
    return std::nullopt;
  }

  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);
  rewritten.emplace_back("--");
  append_remaining_args(rewritten, args, 0);
  return rewritten;
}

// [GNU] echo.c never uses getopt: it scans leading arguments made solely
// of 'e', 'E', 'n' characters and stops option processing at the first
// argument outside that set, echoing it as data along with the rest of the
// line (just_echo). "--" is not an option terminator for GNU echo, so
// `echo -- --` prints "-- --" and `echo --help x` prints "--help x".
// The WinuxCmd -u/--upper and -r/--repeat extensions keep working by
// counting them as option arguments during the scan.
auto rewrite_echo_gnu_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  // [GNU] --help/--version are honored only as the sole argument.
  if (args.size() == 1 && (args[0] == "--help" || args[0] == "--version")) {
    return std::nullopt;
  }

  auto is_nEe_option = [](std::string_view arg) {
    return arg.size() > 1 && arg[0] == '-' && arg[1] != '-' &&
           std::ranges::all_of(arg.substr(1), [](char c) {
             return c == 'n' || c == 'e' || c == 'E';
           });
  };

  size_t options_end = 0;
  while (options_end < args.size()) {
    const std::string_view arg = args[options_end];
    if (is_nEe_option(arg) || arg == "-u" || arg == "--upper" ||
        arg.starts_with("--repeat=") ||
        (arg.size() > 2 && arg.starts_with("-r"))) {
      ++options_end;
      continue;
    }
    if (arg == "-r" || arg == "--repeat") {
      // The repeat count occupies the next argument; keep them together.
      options_end += 2;
      continue;
    }
    break;
  }

  if (options_end >= args.size()) {
    return std::nullopt;
  }

  // Everything from options_end on is literal data; guard it behind a "--"
  // terminator so the generic parser keeps "--" and "-x" as positionals.
  std::vector<std::string> rewritten;
  rewritten.reserve(args.size() + 1);
  for (size_t i = 0; i < options_end; ++i) {
    rewritten.emplace_back(args[i]);
  }
  rewritten.emplace_back("--");
  for (size_t i = options_end; i < args.size(); ++i) {
    rewritten.emplace_back(args[i]);
  }
  return rewritten;
}

auto rewrite_echo_args(std::span<std::string_view> args)
    -> std::optional<std::vector<std::string>> {
  if (echo_posixly_correct_literal_mode("echo", args)) {
    return rewrite_echo_posix_args("echo", args);
  }
  return rewrite_echo_gnu_args(args);
}

auto echo_standard_interception_enabled(std::span<std::string_view> args)
    -> bool {
  return !echo_posixly_correct_literal_mode("echo", args);
}

auto default_standard_interception_enabled(std::span<std::string_view>)
    -> bool {
  return true;
}

// [GNU] nohup owns only the options before the wrapped command: coreutils
// nohup.c parses with a leading-'+' getopt, so option processing ends at
// the first non-option argument.  --help/--version appearing after the
// command name belong to the child (`nohup echo --help` must run
// `echo --help`, not print nohup's help), so interception is enabled only
// when one of them sits in nohup's own option prefix.
auto nohup_standard_interception_enabled(std::span<std::string_view> args)
    -> bool {
  for (const auto &arg : args) {
    if (arg == "--") {
      // "--" ends nohup's own option run; the rest belongs to the child.
      return false;
    }
    if (arg.size() < 2 || arg[0] != '-') {
      // First non-option token: the wrapped command's name.
      return false;
    }
    if (arg == "--help" || arg == "--version") {
      return true;
    }
  }
  return false;
}

auto command_owned_help_interception_disabled(std::span<std::string_view>)
    -> bool {
  return false;
}

auto dispatch_wpm_help_version(const CommandEntryErased &entry,
                               std::span<std::string_view> args)
    -> std::optional<int> {
  for (const auto &arg : args) {
    if (arg == "--help") {
      return entry.handler(std::span<std::string_view>{});
    }
    if (arg == "--version" || arg == "-V") {
      std::array<std::string_view, 1> version_args{"version"};
      return entry.handler(version_args);
    }
  }

  return std::nullopt;
}

auto append_rewrite_hook(CommandBehavior &behavior, ArgsRewriteHook hook)
    -> void {
  if (behavior.rewrite_hook_count < behavior.rewrite_hooks.size()) {
    behavior.rewrite_hooks[behavior.rewrite_hook_count++] = hook;
  }
}

auto behavior_for(std::string_view name) -> CommandBehavior {
  CommandBehavior behavior;
  behavior.standard_interception_enabled =
      default_standard_interception_enabled;

  if (name == "env" || name == "nice" || name == "stdbuf" ||
      name == "timeout") {
    behavior.parse_error_exit_code = 125;
  } else if (name == "nohup") {
    behavior.parse_error_exit_code = is_posixly_correct() ? 127 : 125;
    behavior.standard_interception_enabled =
        nohup_standard_interception_enabled;
  } else if (name == "printenv" || name == "tty" || name == "sort" ||
             name == "ls" || name == "dir" || name == "vdir" ||
             name == "getopt" || name == "expr" || name == "test" ||
             name == "[") {
    // [GNU] these commands exit 2 on option/usage errors.
    behavior.parse_error_exit_code = 2;
  }

  if (name == "head") {
    behavior.validate_args = head_invalid_obsolete_count_context;
    behavior.validation_prefix = "head";
    append_rewrite_hook(behavior, rewrite_head_obsolete_args);
  } else if (name == "tail") {
    behavior.validate_args = tail_invalid_obsolete_count_context;
    behavior.validation_prefix = "tail";
    append_rewrite_hook(behavior, rewrite_tail_obsolete_args);
  } else if (name == "chmod") {
    append_rewrite_hook(behavior, rewrite_chmod_args);
  } else if (name == "chattr") {
    append_rewrite_hook(behavior, rewrite_chattr_args);
  } else if (name == "nice") {
    append_rewrite_hook(behavior, rewrite_nice_args);
  } else if (name == "pr") {
    append_rewrite_hook(behavior, rewrite_pr_args);
  } else if (name == "fold") {
    append_rewrite_hook(behavior, rewrite_fold_obsolete_args);
  } else if (name == "fmt") {
    append_rewrite_hook(behavior, rewrite_fmt_obsolete_args);
  } else if (name == "echo") {
    append_rewrite_hook(behavior, rewrite_echo_args);
    behavior.standard_interception_enabled = echo_standard_interception_enabled;
  } else if (name == "wpm") {
    behavior.special_dispatch = dispatch_wpm_help_version;
  }

  // [GNU] printf/test/[ recognize --help/--version only when one of them
  // is the sole argument; in any other position the token is data.
  if (name == "printf" || name == "test" || name == "[") {
    behavior.help_version_only_when_sole_argument = true;
  }

  // These commands own structured help output and translate it themselves.
  // Let their handlers receive --help instead of the generic metadata path.
  if (name == "top" || name == "mpicalc" || name == "tzset") {
    behavior.standard_interception_enabled =
        command_owned_help_interception_disabled;
  }

  return behavior;
}

auto replace_effective_args(std::vector<std::string> rewritten,
                            std::vector<std::string> &storage,
                            std::vector<std::string_view> &views)
    -> std::span<std::string_view> {
  storage = std::move(rewritten);
  views.clear();
  views.reserve(storage.size());
  for (const auto &arg : storage) {
    views.emplace_back(arg);
  }
  return std::span<std::string_view>(views);
}

// Internal registry implementation class
class RegistryImpl {
  std::unordered_map<std::string_view, CommandEntryErased> registry_;

 public:
  static auto parse_error_exit_code(std::string_view name) -> int {
    return behavior_for(name).parse_error_exit_code;
  }

  // Register a command with compile-time metadata
  template <size_t N>
  void add(std::string_view name, const cmd::meta::CommandMeta<N> &meta,
           CommandInvoker handler) {
    // compile-time meta registry
    cmd::meta::Registry::register_command(name, meta);

    registry_.emplace(
        name, CommandEntryErased{meta.options(), handler, meta.brief_desc()});
  }

  // Dispatch command execution
  int run(std::string_view cmdName, std::span<std::string_view> args) {
    auto it = registry_.find(cmdName);
    if (it == registry_.end()) {
      safeErrorPrintLn(winux::i18n::format("core.error.command_not_found",
                                           "winuxcmd: command not found: {}",
                                           cmdName));
      return 127;
    }

    const CommandBehavior behavior = behavior_for(cmdName);

    if (behavior.validate_args != nullptr) {
      if (auto invalid_arg = behavior.validate_args(args)) {
        safeErrorPrintLn(
            winux::i18n::format("core.error.invalid_option_context",
                                "{}: option used in invalid context -- {}",
                                behavior.validation_prefix, *invalid_arg));
        return behavior.parse_error_exit_code;
      }
    }

    std::vector<std::string> rewritten_storage;
    std::vector<std::string_view> rewritten_views;
    std::span<std::string_view> effective_args = args;

    for (size_t i = 0; i < behavior.rewrite_hook_count; ++i) {
      if (auto hook = behavior.rewrite_hooks[i]) {
        if (auto rewritten = hook(effective_args)) {
          effective_args = replace_effective_args(
              std::move(*rewritten), rewritten_storage, rewritten_views);
        }
      }
    }

    // Get meta data from the command
    auto options = it->second.options;  // std::span<const OptionMeta>

    // GNU getopt_long accepts any unambiguous abbreviation of a long option.
    // Normalise abbreviations against the command's declared long options plus
    // the implicit --help/--version, so hand-rolled option parsers get the
    // same behaviour. Commands whose leading arguments are data strings are
    // excluded (e.g. `echo --hel` must print "--hel", not help).
    {
      // [GNU] printf also belongs here: printf.c parses options directly
      // rather than via getopt_long, precisely so that abbreviations such
      // as "--hel" are treated as the format string, not as --help.
      static constexpr std::string_view kLiteralArgCommands[] = {
          "echo", "yes", "test", "[", "true", "false", "printf"};
      const bool literal_args = std::ranges::any_of(
          kLiteralArgCommands, [cmdName](auto n) { return n == cmdName; });
      if (!literal_args) {
        std::vector<std::string> abbrev_storage;
        bool end_of_options = false;
        std::optional<std::string> ambiguous;
        for (std::string_view arg : effective_args) {
          if (end_of_options || arg.size() <= 2 || !arg.starts_with("--")) {
            if (arg == "--") end_of_options = true;
            abbrev_storage.emplace_back(arg);
            continue;
          }
          std::string_view name = arg;
          std::string_view suffix;
          if (auto eq = arg.find('='); eq != std::string_view::npos) {
            name = arg.substr(0, eq);
            suffix = arg.substr(eq);
          }
          if (name.size() <= 2) {
            abbrev_storage.emplace_back(arg);
            continue;
          }
          bool exact = (name == "--help" || name == "--version");
          for (const auto &m : options) {
            exact = exact || m.long_name == name;
          }
          if (exact) {
            abbrev_storage.emplace_back(arg);
            continue;
          }
          std::vector<std::string_view> matches;
          auto consider = [&matches](std::string_view candidate,
                                     std::string_view prefix) {
            if (candidate.size() > prefix.size() &&
                candidate.starts_with(prefix) &&
                std::ranges::find(matches, candidate) == matches.end()) {
              matches.push_back(candidate);
            }
          };
          for (const auto &m : options) consider(m.long_name, name);
          consider(std::string_view("--help"), name);
          consider(std::string_view("--version"), name);
          if (matches.empty()) {
            abbrev_storage.emplace_back(arg);
            continue;
          }
          if (matches.size() > 1) {
            std::string msg = "option '" + std::string(name) +
                              "' is ambiguous; possibilities:";
            for (std::string_view p : matches) {
              msg += " '";
              msg += p;
              msg += "'";
            }
            ambiguous = std::move(msg);
            break;
          }
          abbrev_storage.push_back(std::string(matches[0]) +
                                   std::string(suffix));
        }
        if (ambiguous) {
          safeErrorPrintLn(std::string(cmdName) + ": " + *ambiguous);
          safeErrorPrintLn(winux::i18n::format(
              "common.try_help", "Try '{} --help' for more information.",
              cmdName));
          return behavior.parse_error_exit_code;
        }
        effective_args = replace_effective_args(
            std::move(abbrev_storage), rewritten_storage, rewritten_views);
      }
    }

    if (behavior.special_dispatch != nullptr) {
      if (auto status = behavior.special_dispatch(it->second, effective_args)) {
        return *status;
      }
    }

    // Check if it contains help. [GNU] "--" ends option processing, so a
    // "--help" after it is data (e.g. `echo -- --help` must print
    // "--help", not show help) — same rule wants_standard_version uses.
    // printf/test/[ narrow it further: their --help/--version are honored
    // only as the sole argument, so `printf '%s\n' --help` prints
    // "--help" and `test --help x` is an expression error, not help.
    bool wants_help = false;
    if (behavior.standard_interception_enabled(args)) {
      if (behavior.help_version_only_when_sole_argument) {
        wants_help =
            effective_args.size() == 1 && effective_args[0] == "--help";
      } else {
        for (const auto &arg : effective_args) {
          if (arg == "--") {
            break;
          }
          if (arg == "--help") {
            wants_help = true;
            break;
          }
        }
      }
    }

    if (wants_help) {
      cmd::meta::Registry::print_help(cmdName);
      return 0;
    }

    // Same gate as --help above: commands that opt out of standard
    // interception (e.g. nohup, whose options end at the wrapped command)
    // must not have their operands scanned for --version either.
    bool wants_version = false;
    if (behavior.standard_interception_enabled(args)) {
      if (behavior.help_version_only_when_sole_argument) {
        wants_version =
            effective_args.size() == 1 && effective_args[0] == "--version";
      } else {
        wants_version =
            wants_standard_version(cmdName, effective_args, options);
      }
    }
    if (wants_version) {
      // [GNU] --version prints a multi-line block in the shape of
      // "cmd (suite) version" + copyright/license/warranty/author (#1044).
      safePrintLn(std::string(cmdName) + " (WinuxCmd) " +
                  std::string(WinuxCmd::VERSION_STRING));
      safePrintLn("Copyright (C) 2026 WinuxCmd");
      safePrintLn("License MIT <https://opensource.org/license/mit>.");
      safePrintLn(
          "This is free software: you are free to change and "
          "redistribute it.");
      safePrintLn("There is NO WARRANTY, to the extent permitted by law.");
      safePrintLn("");
      safePrintLn("Written by WinuxCmd contributors.");
      return 0;
    }

    return it->second.handler(effective_args);
  }

  // Print command help
  void help(std::string_view cmdName) {
    cmd::meta::Registry::print_help(cmdName);
  }

  // Get man page for a command
  std::string man(std::string_view cmdName) {
    return cmd::meta::Registry::get_man(cmdName);
  }

  // Get all registered command names
  std::vector<std::pair<std::string_view, std::string_view>> list() {
    std::vector<std::pair<std::string_view, std::string_view>> commands;
    commands.reserve(registry_.size());
    for (const auto &[name, entry] : registry_) {
      commands.emplace_back(name, entry.brief_desc);
    }
    return commands;
  }

  // Get options for a specific command (for completion)
  std::vector<OptionInfo> command_options(std::string_view cmdName) {
    auto it = registry_.find(cmdName);
    if (it == registry_.end()) return {};
    auto opts = it->second.options;
    std::vector<OptionInfo> result;
    result.reserve(opts.size());
    for (const auto &opt : opts) {
      result.push_back({std::string(opt.short_name), std::string(opt.long_name),
                        std::string(opt.description)});
    }
    return result;
  }
};

// Get singleton instance
inline RegistryImpl &getImpl() {
  static RegistryImpl instance;
  return instance;
}

// Static interface class
export class CommandRegistry {
 public:
  // Template method for registering commands with compile-time metadata
  template <size_t N>
  static void registerCommand(std::string_view name,
                              const cmd::meta::CommandMeta<N> &meta,
                              CommandInvoker handler) {
    getImpl().add<N>(name, meta, handler);
  }

  static int parseErrorExitCode(std::string_view name) noexcept {
    return RegistryImpl::parse_error_exit_code(name);
  }

  // Dispatch command execution (public interface)
  static int dispatch(std::string_view cmdName,
                      std::span<std::string_view> args) noexcept {
    return getImpl().run(cmdName, args);
  }

  // Print command help (public interface)
  static void printHelp(std::string_view cmdName) noexcept {
    getImpl().help(cmdName);
  }

  // Get all registered command names (public interface)
  static std::vector<std::pair<std::string_view, std::string_view>>
  getAllCommands() noexcept {
    return getImpl().list();
  }

  // Get options for a command (for completion)
  static std::vector<OptionInfo> getCommandOptions(
      std::string_view cmdName) noexcept {
    return getImpl().command_options(cmdName);
  }

  // Check whether a command is registered.
  static bool hasCommand(std::string_view cmdName) noexcept {
    auto all = getImpl().list();
    return std::ranges::any_of(
        all, [cmdName](const auto &item) { return item.first == cmdName; });
  }

  // Print man page for a command
  static std::string getManPage(std::string_view cmdName) noexcept {
    return getImpl().man(cmdName);
  }
};
