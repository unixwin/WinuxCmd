// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module core:opt;

import std;
import :cmd_meta;

export using OptionValue = std::variant<bool, int, std::string>;

export struct OptionOccurrence {
  size_t index = 0;
  OptionValue value;
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
  void set(size_t index, OptionValue v) {
    occurrences_.push_back(OptionOccurrence{index, v});
    values_[index] = std::move(v);
    present_.set(index);
  }

  bool has(size_t index) const { return present_.test(index); }

  template <typename T>
  T get(size_t index, T default_value = {}) const {
    if (!present_.test(index)) return default_value;

    if (auto p = std::get_if<T>(&values_[index])) return *p;

    return default_value;
  }

  template <typename T>
  std::vector<T> get_all(size_t index) const {
    std::vector<T> values;
    for (const auto& occurrence : occurrences_) {
      if (occurrence.index != index) continue;
      if (auto p = std::get_if<T>(&occurrence.value)) {
        values.push_back(*p);
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

  void set(size_t index, OptionValue v) {
    occurrences_.push_back(OptionOccurrence{index, std::move(v)});
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
  // Stop option parsing after this many positional operands. A value of zero
  // keeps the traditional parser behavior.
  size_t stop_options_after_positionals = 0;
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

  auto parse_int_value = [&](std::string_view option, std::string_view value,
                             int& out) -> bool {
    std::string str(value);
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), out);
    if (ec != std::errc() || ptr != str.data() + str.size()) {
      set_invalid_argument(option, value);
      return false;
    }
    return true;
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
            set_unrecognized_option(name);
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
          result.options.set(idx, true);
          break;

        case OptionType::Int: {
          int v = 0;
          std::string str;

          if (!value.empty()) {
            // --int=123
            str = std::string(value);
          } else {
            // --int 123
            if (i + 1 >= args.size()) {
              set_missing_argument(name);
              return result;
            }
            str = std::string(args[++i]);
          }

          auto [ptr, ec] =
              std::from_chars(str.data(), str.data() + str.size(), v);
          if (ec != std::errc() || ptr != str.data() + str.size()) {
            set_invalid_argument(name, str);
            return result;
          }

          result.options.set(idx, v);
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

        case OptionType::OptionalInt: {
          if (!has_inline_value) {
            result.options.set(idx, -1);
            break;
          }

          int v = 0;
          std::string str(value);
          auto [ptr, ec] =
              std::from_chars(str.data(), str.data() + str.size(), v);
          if (ec != std::errc() || ptr != str.data() + str.size()) {
            set_invalid_argument(name, str);
            return result;
          }

          result.options.set(idx, v);
          break;
        }

        case OptionType::OptionalString:
          result.options.set(
              idx, has_inline_value ? std::string(value) : std::string());
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
            result.options.set(idx, true);
            break;
          case OptionType::Int: {
            int v = 0;
            std::string str;
            if (!exact_value.empty()) {
              str = std::string(exact_value);
            } else {
              if (i + 1 >= args.size()) {
                set_missing_argument(exact_name);
                return result;
              }
              str = std::string(args[++i]);
            }

            auto [ptr, ec] =
                std::from_chars(str.data(), str.data() + str.size(), v);
            if (ec != std::errc() || ptr != str.data() + str.size()) {
              set_invalid_argument(exact_name, str);
              return result;
            }

            result.options.set(idx, v);
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
          case OptionType::OptionalInt: {
            if (!exact_has_inline_value) {
              result.options.set(idx, -1);
              break;
            }

            int v = 0;
            std::string str(exact_value);
            auto [ptr, ec] =
                std::from_chars(str.data(), str.data() + str.size(), v);
            if (ec != std::errc() || ptr != str.data() + str.size()) {
              set_invalid_argument(exact_name, str);
              return result;
            }

            result.options.set(idx, v);
            break;
          }
          case OptionType::OptionalString:
            result.options.set(idx, exact_has_inline_value
                                        ? std::string(exact_value)
                                        : std::string());
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
                if ((m.type == OptionType::String &&
                     policy.allow_short_attached_values) ||
                    (m.type == OptionType::OptionalString &&
                     policy.allow_optional_short_attached_values)) {
                  glued_to_value_option = true;
                  break;
                }
              }
            }
          }
          if (!glued_to_value_option) {
            result.positionals.push_back(arg);
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
              numeric_meta->type != OptionType::OptionalInt) {
            set_unrecognized_option(arg);
            return result;
          }

          int v = 0;
          if (!parse_int_value(numeric_meta->short_name, arg.substr(1), v)) {
            return result;
          }
          result.options.set(numeric_meta->index, v);
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

            if (meta->type == OptionType::Int) {
              int v = 0;
              auto [ptr, ec] =
                  std::from_chars(str.data(), str.data() + str.size(), v);

              if (ec != std::errc() || ptr != str.data() + str.size()) {
                std::array<char, 3> short_name = {'-', ch, '\0'};
                set_invalid_argument(std::string_view(short_name.data(), 2),
                                     str);
                return result;
              }

              result.options.set(idx, v);
            } else {
              result.options.set(idx, str);
            }

            pos = arg.size();
            break;
          }

          case OptionType::OptionalInt: {
            if (!policy.allow_optional_short_attached_values ||
                pos + 1 >= arg.size()) {
              result.options.set(idx, -1);
              pos = arg.size();
              break;
            }

            std::string str(arg.substr(pos + 1));
            int v = 0;
            auto [ptr, ec] =
                std::from_chars(str.data(), str.data() + str.size(), v);
            if (ec != std::errc() || ptr != str.data() + str.size()) {
              std::array<char, 3> short_name = {'-', ch, '\0'};
              set_invalid_argument(std::string_view(short_name.data(), 2), str);
              return result;
            }

            result.options.set(idx, v);
            pos = arg.size();
            break;
          }

          case OptionType::OptionalString: {
            std::string str;
            if (policy.allow_optional_short_attached_values &&
                pos + 1 < arg.size()) {
              str = std::string(arg.substr(pos + 1));
            }
            result.options.set(idx, std::move(str));
            pos = arg.size();
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
    result.positionals.push_back(arg);
    if (policy.stop_options_after_positionals != 0 &&
        result.positionals.size() >= policy.stop_options_after_positionals) {
      end_of_options = true;
    }
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
      result.options.set(occurrence.index, std::move(occurrence.value));
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
