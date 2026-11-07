// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:opt;

import std;
import :cmd_meta;

// [GNU] Integer option values are stored at int64 width (#1509): GNU's
// numeric option parsers (xstrtoimax/xstrtoumax in grep, coreutils,
// findutils, diffutils) read into intmax_t/uintmax_t, so a 32-bit store
// rejected values GNU accepts (grep -m 3000000000). Consumers that
// still model an int read through get<int>, which saturates the stored
// value into the int range; consumers whose oracle is 64-bit read
// get<std::int64_t> and keep the full width.
//
// [GNU] #1519: options declared UInt/OptionalUInt are the xstrtoumax
// call sites (locate -l, numfmt --header, uniq's counts): a value in
// (INT64_MAX, UINT64_MAX] is stored exactly as std::uint64_t instead
// of failing the parse. The parse is signed-first, so an in-range
// negative still arrives as std::int64_t and the consumer's own
// negative diagnostic keeps its shape; only a positive out-of-range
// value takes the unsigned alternative. Int/OptionalInt options are
// unchanged: their oracles are signed or bounded (numfmt --padding,
// ptx -w reject the band in GNU too), so the band still errors there.
// Readers bridge like the int read below: get<std::uint64_t> widens a
// non-negative signed store, get<std::int64_t>/get<int> saturate an
// unsigned store.
export using OptionValue =
    std::variant<bool, std::int64_t, std::uint64_t, std::string>;

export struct OptionOccurrence {
  size_t index = 0;
  OptionValue value;
  // [GNU] getopt distinguishes an option that carries an attached value
  // ("--follow=name", "--color=", "-i.bak") from a bare occurrence whose
  // optional argument is absent (optarg == nullptr: "--follow",
  // "--color", "-i"). OptionalString occurrences used to store "" for
  // both shapes, so consumers could not tell "--follow" (descriptor
  // mode) from "--follow=" (an empty value argmatch must reject)
  // (#1513). The parser now records the distinction here; the value
  // itself stays "" for both shapes so get<std::string> consumers see
  // no change. Meaningful only for OptionalString occurrences.
  bool value_attached = true;
};

export template <size_t N>
class ParsedOptions {
 private:
  std::array<OptionValue, N> values_{};
  std::bitset<N> present_;
  std::vector<OptionOccurrence> occurrences_;

 public:
  constexpr ParsedOptions() = default;

  // Simple set method
  void set(size_t index, OptionValue v, bool value_attached = true) {
    occurrences_.push_back(OptionOccurrence{index, v, value_attached});
    values_[index] = std::move(v);
    present_.set(index);
  }

  bool has(size_t index) const { return present_.test(index); }

  template <typename T>
  T get(size_t index, T default_value = {}) const {
    if (!present_.test(index)) return default_value;

    // The exact-type read must stay behind if constexpr: std::get_if<T>
    // requires T to be one of the variant's alternatives (a hard
    // static_assert on MSVC), so instantiating it for a bridged type
    // such as int fails compilation even though the branch would never
    // be taken at runtime.
    if constexpr (std::is_same_v<T, bool> ||
                  std::is_same_v<T, std::int64_t> ||
                  std::is_same_v<T, std::uint64_t> ||
                  std::is_same_v<T, std::string>) {
      if (auto p = std::get_if<T>(&values_[index])) return *p;
    }

    // #1519: cross-width bridges for the unsigned store. A signed read
    // of a band value saturates at the signed maximum (the value is
    // beyond the reader's range by construction); an unsigned read of
    // a signed store widens non-negative values and leaves a negative
    // store at the default rather than wrapping it.
    if constexpr (std::is_same_v<T, std::int64_t>) {
      if (auto p = std::get_if<std::uint64_t>(&values_[index])) {
        return static_cast<std::int64_t>(std::min(
            *p, static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())));
      }
    }
    if constexpr (std::is_same_v<T, std::uint64_t>) {
      if (auto p = std::get_if<std::int64_t>(&values_[index])) {
        if (*p >= 0) return static_cast<std::uint64_t>(*p);
      }
    }

    // #1509: integer occurrences are stored as std::int64_t; an int
    // consumer saturates into the int range instead of failing, so
    // un-migrated consumers keep working (their own range checks still
    // see a value outside any sub-int bound they validate).
    if constexpr (std::is_same_v<T, int>) {
      if (auto p = std::get_if<std::int64_t>(&values_[index])) {
        return static_cast<int>(std::clamp(
            *p,
            static_cast<std::int64_t>(std::numeric_limits<int>::min()),
            static_cast<std::int64_t>(std::numeric_limits<int>::max())));
      }
      // #1519: same saturation from an unsigned store.
      if (auto p = std::get_if<std::uint64_t>(&values_[index])) {
        return static_cast<int>(std::min(
            *p, static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max())));
      }
    }

    return default_value;
  }

  template <typename T>
  std::vector<T> get_all(size_t index) const {
    std::vector<T> values;
    for (const auto& occurrence : occurrences_) {
      if (occurrence.index != index) continue;
      if constexpr (std::is_same_v<T, bool> ||
                    std::is_same_v<T, std::int64_t> ||
                    std::is_same_v<T, std::uint64_t> ||
                    std::is_same_v<T, std::string>) {
        if (auto p = std::get_if<T>(&occurrence.value)) {
          values.push_back(*p);
          continue;
        }
      }
      // #1519: same cross-width bridges as get<T> above.
      if constexpr (std::is_same_v<T, std::int64_t>) {
        if (auto p = std::get_if<std::uint64_t>(&occurrence.value)) {
          values.push_back(static_cast<std::int64_t>(std::min(
              *p, static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max()))));
          continue;
        }
      }
      if constexpr (std::is_same_v<T, std::uint64_t>) {
        if (auto p = std::get_if<std::int64_t>(&occurrence.value)) {
          if (*p >= 0) values.push_back(static_cast<std::uint64_t>(*p));
          continue;
        }
      }
      // #1509: same int saturation bridge as get<int> above.
      if constexpr (std::is_same_v<T, int>) {
        if (auto p = std::get_if<std::int64_t>(&occurrence.value)) {
          values.push_back(static_cast<int>(std::clamp(
              *p,
              static_cast<std::int64_t>(std::numeric_limits<int>::min()),
              static_cast<std::int64_t>(std::numeric_limits<int>::max()))));
        } else if (auto p =
                       std::get_if<std::uint64_t>(&occurrence.value)) {
          // #1519: same saturation from an unsigned store.
          values.push_back(static_cast<int>(std::min(
              *p, static_cast<std::uint64_t>(
                      std::numeric_limits<int>::max()))));
        }
      }
    }
    return values;
  }

  std::span<const OptionOccurrence> occurrences() const { return occurrences_; }
};

export class ParsedOptionsRuntime {
 private:
  std::vector<OptionOccurrence> occurrences_;

 public:
  ParsedOptionsRuntime() = default;

  void reset(size_t) { occurrences_.clear(); }

  void set(size_t index, OptionValue v, bool value_attached = true) {
    occurrences_.push_back(
        OptionOccurrence{index, std::move(v), value_attached});
  }

  std::span<OptionOccurrence> occurrences() { return occurrences_; }
  std::span<const OptionOccurrence> occurrences() const { return occurrences_; }
};

export template <size_t N>
struct ParseResult {
  ParsedOptions<N> options;
  std::vector<std::string_view> positionals;
  bool ok = true;
  std::string error_message;
};

export struct ParseResultRuntime {
  ParsedOptionsRuntime options;
  std::vector<std::string_view> positionals;
  bool ok = true;
  std::string error_message;
};

export struct OptionParsePolicy {
  bool recognize_double_dash = true;
  bool allow_long_options = true;
  bool allow_long_equals = true;
  bool allow_single_dash_long_options = true;
  bool allow_numeric_short_options = true;
  bool allow_short_option_clusters = true;
  bool allow_short_attached_values = true;
  bool allow_optional_short_attached_values = true;
  // Some utilities accept operands such as -5 or -n after their syntax
  // establishes that the remaining arguments are operands.
  bool allow_unknown_short_options_as_positionals = false;
  // Mirror for long options: commands that parse argv themselves (procps
  // kill) report unknown "--token" arguments with their own diagnostic, so
  // the token must reach the command body as an operand.
  bool allow_unknown_long_options_as_positionals = false;
  // Stop option parsing after this many positional operands. A value of zero
  // keeps the traditional parser behavior.
  size_t stop_options_after_positionals = 0;
  // #1304: POSIXLY_CORRECT stops getopt-style permutation at the first
  // operand. Set by option_policy_for_command (presence of the variable
  // counts, per glibc); commands whose grammar is not getopt-style opt
  // out there, and the dispatcher pre-passes share this flag.
  bool posixly_correct = false;
  // [GNU] fmt accepts the obsolete "-WIDTH" width form only in argv[1]; a
  // digit option anywhere else is diagnosed by fmt.c itself as
  //   invalid option -- N; -WIDTH is recognized only when it is the first
  bool obsolete_numeric_width_hint = false;
};

export ParseResultRuntime parse_command_runtime(
    std::span<std::string_view> args,
    std::span<const cmd::meta::OptionMeta> metas, OptionParsePolicy policy) {
  ParseResultRuntime result;
  result.options.reset(metas.size());
  using cmd::meta::OptionType;

  bool end_of_options = false;

  // #1304: with POSIXLY_CORRECT, getopt stops at the first operand even
  // for commands that otherwise keep scanning (their configured stop
  // narrows to 1); without it the configured stop applies unchanged.
  const size_t effective_stop_after_positionals =
      policy.posixly_correct
          ? (policy.stop_options_after_positionals == 0
                 ? 1
                 : std::min(policy.stop_options_after_positionals,
                            size_t{1}))
          : policy.stop_options_after_positionals;

  auto push_positional = [&](std::string_view value) -> void {
    result.positionals.push_back(value);
    if (effective_stop_after_positionals != 0 &&
        result.positionals.size() >= effective_stop_after_positionals) {
      end_of_options = true;
    }
  };

  auto take_terminated_string = [&](size_t& i) -> std::string {
    std::string value;
    while (i + 1 < args.size()) {
      std::string_view part = args[++i];
      if (!value.empty()) value.push_back(' ');
      value.append(part.data(), part.size());
      if (part == ";" || part == "+") break;
    }
    return value;
  };

  // [GNU] getopt diagnostics: short options use
  //   "invalid option -- 'x'" / "option requires an argument -- 'x'"
  // while long options use
  //   "unrecognized option '--x'" / "option '--x' requires an argument".
  auto set_unrecognized_option = [&](std::string_view option) -> void {
    result.ok = false;
    if (policy.obsolete_numeric_width_hint && option.size() >= 2 &&
        option[0] == '-' && option[1] != '-' &&
        std::isdigit(static_cast<unsigned char>(option[1]))) {
      // [GNU] fmt.c diagnoses a digit option outside argv[1] itself rather
      // than via getopt, so the message carries no quotes and adds the hint.
      result.error_message = "invalid option -- " +
                             std::string(option.substr(1, 1)) +
                             "; -WIDTH is recognized only when it is the first";
      return;
    }
    if (!option.starts_with("--") && option.size() >= 2 && option[0] == '-') {
      result.error_message =
          "invalid option -- '" + std::string(option.substr(1, 1)) + "'";
    } else {
      result.error_message =
          "unrecognized option '" + std::string(option) + "'";
    }
  };

  auto set_missing_argument = [&](std::string_view option) -> void {
    result.ok = false;
    if (!option.starts_with("--") && option.size() >= 2 && option[0] == '-') {
      result.error_message = "option requires an argument -- '" +
                             std::string(option.substr(1, 1)) + "'";
    } else {
      result.error_message =
          "option '" + std::string(option) + "' requires an argument";
    }
  };

  auto set_invalid_argument = [&](std::string_view option,
                                  std::string_view value) -> void {
    result.ok = false;
    result.error_message = "invalid argument '" + std::string(value) +
                           "' for '" + std::string(option) + "'";
  };

  // [GNU] getopt_long rejects an attached value on a flag option:
  //   "option '--count' doesn't allow an argument".
  auto set_does_not_allow_argument = [&](std::string_view option) -> void {
    result.ok = false;
    result.error_message = "option '" + std::string(option) +
                           "' doesn't allow an argument";
  };

  // [GNU] #1519: numeric values parse signed-first. Int options stop
  // there; UInt options retry an out-of-range positive value as
  // uint64_t and store it in the unsigned alternative (xstrtoumax).
  // A negative value always lands in the signed store, so consumer
  // diagnostics for negatives keep their shape, and a value beyond
  // uint64_t still fails with the shared invalid-argument error.
  // #1523: UIntClamp options (uniq's size_opt family) additionally
  // accept a positive value past uint64_t, storing UINT64_MAX — the
  // LONGINT_OVERFLOW clamp; every reader bridge then saturates it
  // exactly like GNU's MIN(size, SIZE_MAX).
  // #1527: OptionalUIntClamp options (diff's -c/-u family) carry the
  // same overflow clamp, and their attached EMPTY value ("--context=")
  // stores 0 — GNU's optional_arg parser treats the empty string as
  // zero context lines and keeps diffing (diffutils 3.10 --context=
  // probe 2026-11-07: diff runs, rc=1).
  auto parse_int_value = [&](std::string_view option, std::string_view value,
                             OptionType type, OptionValue& out) -> bool {
    const bool unsigned_path =
        type == OptionType::UInt || type == OptionType::UIntClamp ||
        type == OptionType::OptionalUInt || type == OptionType::OptionalUIntClamp;
    const bool clamp_overflow =
        type == OptionType::UIntClamp || type == OptionType::OptionalUIntClamp;
    std::string str(value);
    if (str.empty() && type == OptionType::OptionalUIntClamp) {
      out = std::int64_t{0};
      return true;
    }
    std::int64_t v = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), v);
    if (ec == std::errc() && ptr == str.data() + str.size()) {
      out = v;
      return true;
    }
    if (unsigned_path && ec == std::errc::result_out_of_range) {
      std::uint64_t u = 0;
      auto [uptr, uec] =
          std::from_chars(str.data(), str.data() + str.size(), u);
      if (uec == std::errc() && uptr == str.data() + str.size()) {
        out = u;
        return true;
      }
      if (clamp_overflow && uec == std::errc::result_out_of_range &&
          uptr == str.data() + str.size()) {
        out = std::numeric_limits<std::uint64_t>::max();
        return true;
      }
    }
    set_invalid_argument(option, value);
    return false;
  };

  auto is_decimal_digits = [](std::string_view value) -> bool {
    return !value.empty() && std::ranges::all_of(value, [](char ch) {
      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    });
  };

  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view arg = args[i];

    // ---------- "--" : end of options ----------
    if (!end_of_options && policy.recognize_double_dash && arg == "--") {
      end_of_options = true;
      continue;
    }

    // ---------- long option ----------
    if (!end_of_options && policy.allow_long_options && arg.starts_with("--")) {
      const cmd::meta::OptionMeta* meta = nullptr;
      std::string_view value;
      std::string_view name = arg;

      size_t eq_pos = arg.find('=');
      bool has_inline_value =
          policy.allow_long_equals && eq_pos != std::string_view::npos;
      if (has_inline_value) {
        name = arg.substr(0, eq_pos);
        value = arg.substr(eq_pos + 1);
      }

      for (const auto& m : metas) {
        if (m.long_name == name) {
          meta = &m;
          break;
        }
      }

      if (!meta) {
        // GNU getopt_long accepts an unambiguous abbreviation of a long
        // option and reports every candidate when it is ambiguous.
        std::vector<std::string_view> possibilities;
        for (const auto& m : metas) {
          if (m.long_name.size() > name.size() &&
              m.long_name.starts_with(name)) {
            possibilities.push_back(m.long_name);
            if (!meta) meta = &m;
          }
        }
        if (possibilities.size() != 1) {
          meta = nullptr;
          if (possibilities.empty()) {
            // Commands whose argument language is not getopt_long-shaped
            // (procps kill parses argv itself) judge unknown long tokens
            // with their own diagnostics, so the token stays an operand
            // for the command body to reject.
            if (policy.allow_unknown_long_options_as_positionals) {
              push_positional(arg);
              continue;
            }
            // #1305: GNU keeps the "=value" in the diagnostic
            // ("unrecognized option '--unknown=x'"); only the ambiguous
            // report below uses the bare name.
            set_unrecognized_option(arg);
          } else {
            std::string msg = "option '" + std::string(name) +
                              "' is ambiguous; possibilities:";
            for (std::string_view p : possibilities) {
              msg += " '";
              msg.append(p);
              msg += "'";
            }
            result.ok = false;
            result.error_message = std::move(msg);
          }
          return result;
        }
      }

      size_t idx = meta->index;

      switch (meta->type) {
        case OptionType::Bool:
          // #1301: a flag option must reject "--flag=value" instead of
          // silently ignoring the value (grep --count=x used to hang).
          if (has_inline_value) {
            set_does_not_allow_argument(name);
            return result;
          }
          result.options.set(idx, true);
          break;

        case OptionType::Int:
        case OptionType::UInt:
        case OptionType::UIntClamp: {
          std::string str;

          // #1302: an explicit empty value ("--opt=") is a value, not a
          // missing one: it must fail here instead of swallowing the
          // next argv as the value.
          if (has_inline_value) {
            // --int=123 (including the explicit empty "--int=")
            str = std::string(value);
          } else {
            // --int 123
            if (i + 1 >= args.size()) {
              set_missing_argument(name);
              return result;
            }
            str = std::string(args[++i]);
          }

          OptionValue v;
          if (!parse_int_value(name, str, meta->type, v)) {
            return result;
          }

          result.options.set(idx, std::move(v));
          break;
        }

        case OptionType::String: {
          if (has_inline_value) {
            // --string=value (including an explicit empty value)
            result.options.set(idx, std::string(value));
          } else {
            // --string value
            if (i + 1 >= args.size()) {
              set_missing_argument(name);
              return result;
            }
            result.options.set(idx, std::string(args[++i]));
          }
          break;
        }

        case OptionType::OptionalInt:
        case OptionType::OptionalUInt:
        case OptionType::OptionalUIntClamp: {
          if (!has_inline_value) {
            result.options.set(idx, std::int64_t{-1});
            break;
          }

          OptionValue v;
          if (!parse_int_value(name, value, meta->type, v)) {
            return result;
          }

          result.options.set(idx, std::move(v));
          break;
        }

        case OptionType::OptionalString:
          result.options.set(
              idx, has_inline_value ? std::string(value) : std::string(),
              has_inline_value);
          break;

        case OptionType::TerminatedString:
          if (has_inline_value) {
            result.options.set(idx, std::string(value));
          } else {
            result.options.set(idx, take_terminated_string(i));
          }
          break;
      }

      continue;
    }

    // ---------- short option(s) ----------
    if (!end_of_options && arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
      // Support GNU-style single-dash multi-char options (e.g. -name)
      const cmd::meta::OptionMeta* exact_meta = nullptr;
      std::string_view exact_value;
      std::string_view exact_name = arg;
      size_t exact_eq_pos = arg.find('=');
      bool exact_has_inline_value =
          policy.allow_long_equals && exact_eq_pos != std::string_view::npos;
      if (exact_has_inline_value) {
        exact_name = arg.substr(0, exact_eq_pos);
        exact_value = arg.substr(exact_eq_pos + 1);
      }

      if (policy.allow_single_dash_long_options) {
        for (const auto& m : metas) {
          if (!m.short_name.empty() && m.short_name == exact_name) {
            exact_meta = &m;
            break;
          }
        }
      }

      if (exact_meta && exact_meta->short_name.size() > 2) {
        size_t idx = exact_meta->index;
        switch (exact_meta->type) {
          case OptionType::Bool:
            // #1301, single-dash long form: same rejection as "--flag=".
            if (exact_has_inline_value) {
              set_does_not_allow_argument(exact_name);
              return result;
            }
            result.options.set(idx, true);
            break;
          case OptionType::Int:
          case OptionType::UInt:
          case OptionType::UIntClamp: {
            std::string str;
            // #1302, single-dash long form: an explicit empty value is
            // a value, not a missing one.
            if (exact_has_inline_value) {
              str = std::string(exact_value);
            } else {
              if (i + 1 >= args.size()) {
                set_missing_argument(exact_name);
                return result;
              }
              str = std::string(args[++i]);
            }

            OptionValue v;
            if (!parse_int_value(exact_name, str, exact_meta->type, v)) {
              return result;
            }

            result.options.set(idx, std::move(v));
            break;
          }
          case OptionType::String:
            if (exact_has_inline_value) {
              result.options.set(idx, std::string(exact_value));
            } else {
              if (i + 1 >= args.size()) {
                set_missing_argument(exact_name);
                return result;
              }
              result.options.set(idx, std::string(args[++i]));
            }
            break;
          case OptionType::OptionalInt:
          case OptionType::OptionalUInt:
          case OptionType::OptionalUIntClamp: {
            if (!exact_has_inline_value) {
              result.options.set(idx, std::int64_t{-1});
              break;
            }

            OptionValue v;
            if (!parse_int_value(exact_name, exact_value,
                                 exact_meta->type, v)) {
              return result;
            }

            result.options.set(idx, std::move(v));
            break;
          }
          case OptionType::OptionalString:
            result.options.set(idx,
                               exact_has_inline_value
                                   ? std::string(exact_value)
                                   : std::string(),
                               exact_has_inline_value);
            break;
          case OptionType::TerminatedString:
            if (exact_has_inline_value) {
              result.options.set(idx, std::string(exact_value));
            } else {
              result.options.set(idx, take_terminated_string(i));
            }
            break;
        }

        continue;
      }

      if (policy.allow_unknown_short_options_as_positionals) {
        bool has_registered_short_name = false;
        for (const auto& m : metas) {
          if (m.short_name == arg) {
            has_registered_short_name = true;
            break;
          }
        }
        if (!has_registered_short_name) {
          // A token glued to a registered short option that takes a value
          // (e.g. kill -lHUP) is an option with an attached value, not a
          // positional operand (uutils #14177 regression guard).
          bool glued_to_value_option = false;
          if (arg.size() > 2) {
            for (const auto& m : metas) {
              if (m.short_name.size() == 2 && arg.starts_with(m.short_name)) {
                // GNU accepts glued numeric values on int options too
                // (diff -U1, head -n5 style) — treat the rest of the
                // token as the option's value for all value-taking types.
                if ((m.type == OptionType::String &&
                     policy.allow_short_attached_values) ||
                    (m.type == OptionType::OptionalString &&
                     policy.allow_optional_short_attached_values) ||
                    (m.type == OptionType::Int &&
                     policy.allow_short_attached_values) ||
                    (m.type == OptionType::UInt &&
                     policy.allow_short_attached_values) ||
                    (m.type == OptionType::UIntClamp &&
                     policy.allow_short_attached_values) ||
                    (m.type == OptionType::OptionalInt &&
                     policy.allow_optional_short_attached_values) ||
                    (m.type == OptionType::OptionalUInt &&
                     policy.allow_optional_short_attached_values) ||
                    (m.type == OptionType::OptionalUIntClamp &&
                     policy.allow_optional_short_attached_values)) {
                  glued_to_value_option = true;
                  break;
                }
              }
            }
          }
          if (!glued_to_value_option) {
            push_positional(arg);
            continue;
          }
        }
      }

      if (policy.allow_numeric_short_options && arg.size() > 1 &&
          is_decimal_digits(arg.substr(1))) {
        const cmd::meta::OptionMeta* numeric_meta = nullptr;
        for (const auto& m : metas) {
          if (m.short_name == "-NUM") {
            numeric_meta = &m;
            break;
          }
        }

        if (numeric_meta) {
          if (numeric_meta->type != OptionType::Int &&
              numeric_meta->type != OptionType::OptionalInt &&
              numeric_meta->type != OptionType::UInt &&
              numeric_meta->type != OptionType::UIntClamp &&
              numeric_meta->type != OptionType::OptionalUInt) {
            set_unrecognized_option(arg);
            return result;
          }

          OptionValue v;
          if (!parse_int_value(numeric_meta->short_name, arg.substr(1),
                               numeric_meta->type, v)) {
            return result;
          }
          result.options.set(numeric_meta->index, std::move(v));
          continue;
        }
      }

      if (!policy.allow_short_option_clusters && arg.size() > 2) {
        set_unrecognized_option(arg);
        return result;
      }

      // iterate each short flag: -abc
      for (size_t pos = 1; pos < arg.size(); ++pos) {
        char ch = arg[pos];

        // #1306: a digit run inside a cluster is the command's -NUM
        // option (GNU split -d5, diff -c5, uniq -D5), not an attached
        // value of the preceding flag. Commands without a -NUM meta
        // fall through to the unrecognized-option diagnostic below.
        if (policy.allow_numeric_short_options &&
            std::isdigit(static_cast<unsigned char>(ch)) != 0) {
          const cmd::meta::OptionMeta* numeric_meta = nullptr;
          for (const auto& m : metas) {
            if (m.short_name == "-NUM") {
              numeric_meta = &m;
              break;
            }
          }
          if (numeric_meta) {
            size_t end = pos;
            while (end < arg.size() &&
                   std::isdigit(static_cast<unsigned char>(arg[end])) != 0) {
              ++end;
            }
            OptionValue v;
            if (!parse_int_value(numeric_meta->short_name,
                                 arg.substr(pos, end - pos),
                                 numeric_meta->type, v)) {
              return result;
            }
            result.options.set(numeric_meta->index, std::move(v));
            pos = end - 1;
            continue;
          }
        }

        const cmd::meta::OptionMeta* meta = nullptr;

        for (const auto& m : metas) {
          if (!m.short_name.empty() && m.short_name.size() == 2 &&
              m.short_name[0] == '-' && m.short_name[1] == ch) {
            meta = &m;
            break;
          }
        }

        if (!meta) {
          std::array<char, 3> short_name = {'-', ch, '\0'};
          set_unrecognized_option(std::string_view(short_name.data(), 2));
          return result;
        }

        size_t idx = meta->index;

        switch (meta->type) {
          // ----- bool: can be grouped -----
          case OptionType::Bool:
            result.options.set(idx, true);
            break;

          // ----- value option -----
          case OptionType::Int:
          case OptionType::UInt:
          case OptionType::UIntClamp:
          case OptionType::String: {
            std::string str;

            if (policy.allow_short_attached_values && pos + 1 < arg.size()) {
              str = std::string(arg.substr(pos + 1));
            } else {
              if (i + 1 >= args.size()) {
                std::array<char, 3> short_name = {'-', ch, '\0'};
                set_missing_argument(std::string_view(short_name.data(), 2));
                return result;
              }
              str = std::string(args[++i]);
            }

            if (meta->type == OptionType::Int ||
                meta->type == OptionType::UInt ||
                meta->type == OptionType::UIntClamp) {
              std::array<char, 3> short_name = {'-', ch, '\0'};
              OptionValue v;
              if (!parse_int_value(std::string_view(short_name.data(), 2),
                                   str, meta->type, v)) {
                return result;
              }

              result.options.set(idx, std::move(v));
            } else {
              result.options.set(idx, str);
            }

            pos = arg.size();
            break;
          }

          case OptionType::OptionalInt:
          case OptionType::OptionalUInt:
          case OptionType::OptionalUIntClamp: {
            // #1306: when a command opts out of short attached values
            // (uniq/diff/split), the flag is recorded with no value and
            // the rest of the cluster keeps parsing as further short
            // options (uniq -Dc, diff -cu) instead of being swallowed
            // or dropped. The attached-value form still ends the
            // cluster for commands that keep it (sed -i.bak).
            if (!policy.allow_optional_short_attached_values ||
                pos + 1 >= arg.size()) {
              result.options.set(idx, std::int64_t{-1});
              break;
            }

            std::string str(arg.substr(pos + 1));
            std::array<char, 3> short_name = {'-', ch, '\0'};
            OptionValue v;
            if (!parse_int_value(std::string_view(short_name.data(), 2),
                                 str, meta->type, v)) {
              return result;
            }

            result.options.set(idx, std::move(v));
            pos = arg.size();
            break;
          }

          case OptionType::OptionalString: {
            // #1306: same rule as OptionalInt above. #1513: a cluster
            // remainder is an attached value (getopt's optarg); a bare
            // flag at the end of the cluster is not.
            if (policy.allow_optional_short_attached_values &&
                pos + 1 < arg.size()) {
              result.options.set(idx, std::string(arg.substr(pos + 1)),
                                 true);
              pos = arg.size();
            } else {
              result.options.set(idx, std::string(), false);
            }
            break;
          }

          case OptionType::TerminatedString: {
            std::string str;
            if (pos + 1 < arg.size()) {
              str = std::string(arg.substr(pos + 1));
            } else {
              str = take_terminated_string(i);
            }
            result.options.set(idx, std::move(str));
            pos = arg.size();
            break;
          }
        }
      }

      continue;
    }

    // ---------- positional ----------
    push_positional(arg);
  }

  return result;
}

export ParseResultRuntime parse_command_runtime(
    std::span<std::string_view> args,
    std::span<const cmd::meta::OptionMeta> metas) {
  return parse_command_runtime(args, metas, OptionParsePolicy{});
}

export template <size_t N>
ParseResult<N> parse_command(std::span<std::string_view> args,
                             const std::array<cmd::meta::OptionMeta, N>& metas,
                             OptionParsePolicy policy) {
  auto runtime = parse_command_runtime(
      args, std::span<const cmd::meta::OptionMeta>(metas.data(), metas.size()),
      policy);

  ParseResult<N> result;
  result.positionals = std::move(runtime.positionals);
  result.ok = runtime.ok;
  result.error_message = std::move(runtime.error_message);

  for (auto& occurrence : runtime.options.occurrences()) {
    if (occurrence.index < N) {
      result.options.set(occurrence.index, std::move(occurrence.value),
                         occurrence.value_attached);
    }
  }

  return result;
}

export template <size_t N>
ParseResult<N> parse_command(
    std::span<std::string_view> args,
    const std::array<cmd::meta::OptionMeta, N>& metas) {
  return parse_command(args, metas, OptionParsePolicy{});
}
