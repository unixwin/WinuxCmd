// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:command_context;
import :opt;

export auto option_policy_for_command(std::string_view command)
    -> OptionParsePolicy {
  OptionParsePolicy policy;
  policy.allow_unknown_short_options_as_positionals =
      command == "printf" || command == "expr" || command == "test" ||
      command == "[" || command == "stty" || command == "kill";
  if (command == "timeout") {
    policy.stop_options_after_positionals = 2;
  }
  if (command == "getopt") {
    // [util-linux] getopt's own options end at the first operand
    // (the optstring); everything after it is data to normalize.
    policy.stop_options_after_positionals = 1;
  }
  if (command == "nohup") {
    // [GNU] coreutils nohup parses with a leading-'+' getopt: option
    // processing stops at the first non-option argument (the wrapped
    // command), so `nohup prog -flag` runs prog with -flag instead of
    // reporting -flag as an invalid nohup option.
    policy.stop_options_after_positionals = 1;
  }
  // [GNU] printf.c and test.c never call getopt: "--help"/"--version" are
  // honored only as the sole argument (handled by the dispatcher), and
  // every other token is an operand.  Long-option tokens therefore stay
  // literal operands instead of producing "unrecognized option" errors,
  // and for printf everything after the format operand is data — a
  // trailing "-v", "-5" or "--help" is an argument, not an option.  The
  // WinuxCmd "-v" extension still parses ahead of the format.
  if (command == "printf") {
    policy.allow_long_options = false;
    policy.stop_options_after_positionals = 1;
  }
  if (command == "test" || command == "[") {
    policy.allow_long_options = false;
  }
  // [GNU] fmt's obsolete "-WIDTH" width is argv[1]-only; digit options in
  // other positions get fmt.c's own diagnostic.
  policy.obsolete_numeric_width_hint = command == "fmt";
  return policy;
}

export struct StringOptionOccurrence {
  std::string_view short_name;
  std::string_view long_name;
  std::string value;
};

export template <size_t N>
struct CommandContext {
  const std::array<cmd::meta::OptionMeta, N>* metas = nullptr;

  ParsedOptions<N> options;
  std::vector<std::string_view> raw_args;
  std::vector<std::string_view> positionals;
  std::string parse_error;

  template <typename T>
  T get(std::string_view name, T default_value) const {
    if (!metas) return default_value;

    for (size_t i = 0; i < N; ++i) {
      if ((*metas)[i].long_name == name || (*metas)[i].short_name == name) {
        return options.get<T>(i, default_value);
      }
    }
    return default_value;
  }

  bool has(std::string_view name) const {
    if (!metas) return false;

    for (size_t i = 0; i < N; ++i) {
      if ((*metas)[i].long_name == name || (*metas)[i].short_name == name) {
        return options.has(i);
      }
    }
    return false;
  }

  template <typename T>
  std::vector<T> get_all(std::string_view name) const {
    if (!metas) return {};

    for (size_t i = 0; i < N; ++i) {
      if ((*metas)[i].long_name == name || (*metas)[i].short_name == name) {
        return options.template get_all<T>(i);
      }
    }
    return {};
  }

  size_t count(std::initializer_list<std::string_view> names) const {
    if (!metas) return 0;

    size_t total = 0;
    for (const auto& occurrence : options.occurrences()) {
      if (occurrence.index >= N) continue;
      const auto& meta = (*metas)[occurrence.index];

      for (auto name : names) {
        if (meta.long_name == name || meta.short_name == name) {
          ++total;
          break;
        }
      }
    }
    return total;
  }

  std::vector<StringOptionOccurrence> string_occurrences(
      std::initializer_list<std::string_view> names) const {
    std::vector<StringOptionOccurrence> out;
    if (!metas) return out;

    for (const auto& occurrence : options.occurrences()) {
      if (occurrence.index >= N) continue;
      const auto& meta = (*metas)[occurrence.index];

      bool wanted = false;
      for (auto name : names) {
        if (meta.long_name == name || meta.short_name == name) {
          wanted = true;
          break;
        }
      }
      if (!wanted) continue;

      if (auto value = std::get_if<std::string>(&occurrence.value)) {
        out.push_back(
            StringOptionOccurrence{meta.short_name, meta.long_name, *value});
      }
    }
    return out;
  }
};

export template <size_t N>
CommandContext<N> make_context(
    std::span<std::string_view> args,
    const std::array<cmd::meta::OptionMeta, N>& metas, bool& ok,
    OptionParsePolicy policy) {
  auto parsed = parse_command(args, metas, policy);
  ok = parsed.ok;

  CommandContext<N> ctx;
  ctx.metas = &metas;
  ctx.options = std::move(parsed.options);
  ctx.raw_args.assign(args.begin(), args.end());
  ctx.positionals = std::move(parsed.positionals);
  ctx.parse_error = std::move(parsed.error_message);

  return ctx;
}

export template <size_t N>
CommandContext<N> make_context(
    std::span<std::string_view> args,
    const std::array<cmd::meta::OptionMeta, N>& metas, bool& ok) {
  return make_context(args, metas, ok, OptionParsePolicy{});
}
