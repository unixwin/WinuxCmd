// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:command_context;
import :opt;

export auto option_policy_for_command(std::string_view command)
    -> OptionParsePolicy {
  OptionParsePolicy policy;
  policy.allow_unknown_short_options_as_positionals =
      command == "printf" || command == "expr" || command == "test" ||
      command == "[" || command == "stty" || command == "kill" ||
      // #1488 rust2 wiring: util-linux rev owns -h/--help and -V/--version
      // that the registration metadata does not spell (`rev -h` prints the
      // util-linux help, rc 0 — rv14 gate row), so unknown short tokens
      // must reach the wired Rust body for its own getopt-shaped
      // diagnostics instead of aborting at the generic parser.
      command == "rev";
  if (command == "kill") {
    // [GNU] procps-ng kill (the differential oracle at /usr/bin/kill) parses
    // argv itself and answers an unknown option with its own
    // "kill: invalid argument X" + usage block; both unknown short and
    // unknown long tokens must reach the command body as operands for
    // that diagnostic (differential kill/12, `kill -p 424242`).
    policy.allow_unknown_long_options_as_positionals = true;
  }
  if (command == "uniq" || command == "diff" || command == "split") {
    // #1306: GNU getopt gives these commands' optional short options no
    // attached value inside a cluster: -D/-c/-d are flags there and the
    // rest of the cluster keeps parsing as further short options
    // (uniq -Dc, diff -cu, split -dl). Digits still reach the -NUM
    // rule (diff -c5, split -d5, uniq -D5), and --opt=value is
    // untouched. Commands that genuinely take a short attached value
    // (sed -i.bak, paste -d,) keep the default.
    policy.allow_optional_short_attached_values = false;
  }
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
  if (command == "stdbuf" || command == "time" || command == "runcon" ||
      command == "chroot") {
    // [GNU] The wrapper applets stop their own option parsing at the
    // first non-option operand: stdbuf/time at COMMAND, runcon at
    // CONTEXT (its command follows as the next operand), chroot at
    // NEWROOT. Everything from the wrapped command on passes through
    // untouched, so `stdbuf -oL ls -l`, `time ls -l`,
    // `runcon ctx ls -l` and `chroot /jail ls -l` run the wrapped
    // command instead of diagnosing its -l as their own option (#1375;
    // same discipline as nohup/xargs below).
    policy.stop_options_after_positionals = 1;
  }
  if (command == "nohup") {
    // [GNU] coreutils nohup parses with a leading-'+' getopt: option
    // processing stops at the first non-option argument (the wrapped
    // command), so `nohup prog -flag` runs prog with -flag instead of
    // reporting -flag as an invalid nohup option.
    policy.stop_options_after_positionals = 1;
  }
  if (command == "env") {
    // [GNU] coreutils env also parses with a leading-'+' getopt: option
    // processing stops at the first non-option argument (an assignment
    // or the wrapped command's name), so `env ./ls -l` runs ls with its
    // own -l instead of reporting -l as an invalid env option, and a
    // leading-dash token after an assignment is the command to run
    // (#1353).
    policy.stop_options_after_positionals = 1;
  }
  if (command == "nice") {
    // [GNU] nice.c parses with a leading-'+' optstring ("+n:"): option
    // processing stops at the first non-option argument (the wrapped
    // command), so `nice -n 5 ls -l` runs ls with its own -l instead of
    // reporting -l as an invalid nice option (#1408).
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
  if (command == "watch") {
    // [GNU procps] watch.c parses with a leading-'+' optstring
    // ("+bCced::ghq:n:prtwvx"): option processing stops at the first
    // non-option argument (the watched command), so `watch ls -l`
    // passes -l to ls instead of reporting it as an invalid watch
    // option (#1127).
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
  if (command == "printenv") {
    // [GNU] printenv.c:99: the optstring is "+iu:0" — the leading
    // '+' stops option parsing at the first non-option argument,
    // so `printenv HOME -0` looks up a variable literally named
    // '-0' (and `--help` after an operand is a variable name too).
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
  if (command == "expand" || command == "unexpand") {
    // [GNU] coreutils 9.4: expand.c:48 optstring "it:0::1::...9::" gives
    // every digit an OPTIONAL argument and expand.c:204-215 passes
    // `optarg - 1` (digit + whole rest of the token) to parse_tab_stops
    // as one tab-list spec; unexpand.c:272 optstring ",0123456789at:"
    // accumulates digits and commas per character. The interior of a
    // digit-led token is therefore command-owned — `expand -4,8` converts
    // (WSL 9.4 probe) where a generic cluster scan would say
    // "invalid option -- ','" — so the token passes whole to the body.
    policy.digit_led_short_tokens_as_positionals = true;
  }
  // [GNU] sed.c owns its whole argument language: its getopt_long run
  // (option/operand permutation, -e/-f in-order compilation, -i[SUF]
  // expansion, --posix) lives in the wired rust2 body
  // (rust2/crates/winux-cmds/src/sed.rs, #1488), which also emits the GNU
  // getopt diagnostics and the full GNU usage block. The generic parser
  // must not consume or reject any token ahead of it (sed -e with no
  // argument, sed -l abc, unknown/ambiguous long options all reach the
  // library verbatim), and the dispatcher's abbreviation/help/version
  // pre-passes stand down with the same flag.
  if (command == "sed") {
    policy.all_args_are_positionals = true;
  }
  // [GNU] echo.c also never calls getopt (its hand-rolled scan consumes
  // only leading [-neE]-only tokens and treats everything else — "-0",
  // "-x", "--upper", a non-sole "--help" — as data). The wired rust2 body
  // (echo.rs, #1488) owns that scan off ctx.raw_args, so the generic
  // parser stays permissive: unknown short tokens and every long token
  // pass through as operands and no echo argv is rejected here. The
  // declared -n/-e/-E (and the retired -u/-r extension spellings) still
  // parse for option-occurrence bookkeeping only; the handler never reads
  // them.
  if (command == "echo") {
    policy.allow_long_options = false;
    policy.allow_unknown_short_options_as_positionals = true;
  }
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
  // [GNU] find owns its whole argument language: every token after the
  // starting points is a predicate judged by find's own parser, and an
  // unknown one must be answered with find's "unknown predicate"
  // diagnostic, never by the generic option parser ("unrecognized
  // option '--zzz'" / "invalid option -- 'b'" for -bogus) or by operand
  // handling ("'-D': No such file or directory").  find has no long
  // options of its own (the only long name, --debug, is handled by
  // find's expression parser as the -D alias), and its single-dash
  // predicates are never clusters, so unknown dash tokens pass through
  // as positionals for find itself to judge (#1380).
  if (command == "find") {
    policy.allow_long_options = false;
    policy.allow_unknown_short_options_as_positionals = true;
  }
  return policy;
}

export struct StringOptionOccurrence {
  std::string_view short_name;
  std::string_view long_name;
  std::string value;
  // [GNU] true when the occurrence carried an attached value
  // ("--opt=value", "--opt=", "-ovalue"); false for a bare OptionalString
  // occurrence, where GNU getopt reports optarg == nullptr (#1513).
  bool has_value = true;
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
        out.push_back(StringOptionOccurrence{meta.short_name, meta.long_name,
                                             *value,
                                             occurrence.value_attached});
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
