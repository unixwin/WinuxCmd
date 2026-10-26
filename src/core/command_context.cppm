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
  if (command == "env") {
    // #1303: GNU env scans options only up to the first non-option
    // token: an assignment or the command name ends option parsing, so
    // `env FOO=bar -i cmd` treats -i as the command and
    // `env echo --version` runs echo. Assignments are positionals to
    // the parser, so stopping after the first positional is exactly
    // this boundary (and it holds with or without POSIXLY_CORRECT).
    policy.stop_options_after_positionals = 1;
  }
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
  if (command == "xargs") {
    // [GNU findutils] xargs's own options stop at the first non-option
    // operand (the utility name); every argument after it passes to the
    // utility verbatim (POSIX xargs operand semantics).  Without this,
    // `xargs cat -n` reports "option requires an argument" and `xargs wc
    // -l` silently consumes -l as xargs's own --max-lines (#1139).
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
  // [GNU] expr.c also parses no options with getopt: a leading "--foo" is an
  // expression token, so `expr --zzz` prints "--zzz" and exits 0 while
  // `expr 1 --help` is a syntax error.  Only the sole-argument spellings
  // reach the dispatcher's --help/--version interception (WinuxCmd #1215).
  if (command == "expr") {
    policy.allow_long_options = false;
  }
  // #1304: POSIXLY_CORRECT stops getopt permutation at the first
  // operand. glibc counts mere presence of the variable (unlike the
  // dispatcher's is_posixly_correct(), which also requires a non-empty
  // value). Commands whose argument grammar is not getopt-style keep
  // permuting: find parses an expression off the raw args, test/[ and
  // expr parse expressions, true/false never call getopt, and dd takes
  // KEY=VALUE operands plus a usage scan that ignores operand order.
  // (env needs no flag: its stop=1 above is the same boundary already.)
  if (command != "find" && command != "test" && command != "[" &&
      command != "expr" && command != "true" && command != "false" &&
      command != "dd") {
    policy.posixly_correct = std::getenv("POSIXLY_CORRECT") != nullptr;
  }
  // [GNU] fmt's obsolete "-WIDTH" width is argv[1]-only; digit options in
  // other positions get fmt.c's own diagnostic.
  policy.obsolete_numeric_width_hint = command == "fmt";
  // [GNU] coreutils truefalse.c never calls getopt: every operand is
  // ignored ("true -x -q --whatever" exits with the plain status and no
  // diagnostics), and --help/--version are honored only as the sole
  // argument — which the dispatcher intercepts before this parser runs.
  // Keep long-option parsing off so "--foo" stays an operand instead of
  // raising "unrecognized option", and let unknown short options pass as
  // operands too ("true -x" must exit 0, not 1, WinuxCmd #1196).
  if (command == "true" || command == "false") {
    policy.allow_long_options = false;
    policy.allow_unknown_short_options_as_positionals = true;
  }
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
