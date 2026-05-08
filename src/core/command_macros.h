/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: command_macros.h
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#pragma once

#include <optional>

#ifdef _MSC_VER
#define CMD_MSG(msg) __pragma(message("[WINUX] " msg))
#else
#define CMD_MSG(msg)
#endif

#define REGISTER_COMMAND(name, cmd_name, cmd_synopsis, cmd_desc, examples,     \
                         see_also, author, copyright, ...)                     \
                                                                               \
  namespace command_##name##_internal {                                        \
    template <typename T>                                                      \
    concept IsOptionMeta =                                                     \
        std::is_same_v<std::remove_cvref_t<T>, cmd::meta::OptionMeta>;         \
                                                                               \
    template <typename T>                                                      \
    concept IsOptionContainer = requires(T t) {                                \
      requires std::is_array_v<T> || requires {                                \
        { t.begin() } -> std::input_iterator;                                  \
        { t.end() } -> std::input_iterator;                                    \
        requires IsOptionMeta<std::iter_value_t<decltype(t.begin())>>;         \
      };                                                                       \
    };                                                                         \
                                                                               \
    template <typename T>                                                      \
    concept IsSingleOption = IsOptionMeta<T>;                                  \
                                                                               \
    template <typename T>                                                      \
    concept IsOptionArray = IsOptionContainer<T> && !IsOptionMeta<T>;          \
                                                                               \
    template <typename... Args>                                                \
    constexpr auto make_option_array_impl(Args && ... args) {                  \
      if constexpr (sizeof...(Args) == 0) {                                    \
        return std::array<cmd::meta::OptionMeta, 0>{};                         \
      } else if constexpr (sizeof...(Args) == 1) {                             \
        using FirstType = std::tuple_element_t<0, std::tuple<Args...>>;        \
        if constexpr (IsSingleOption<FirstType>) {                             \
          return std::array<cmd::meta::OptionMeta, 1>{                         \
              std::forward<Args>(args)...};                                    \
        } else if constexpr (IsOptionArray<FirstType>) {                       \
          return std::forward<FirstType>(args...);                             \
        } else {                                                               \
          static_assert(                                                       \
              IsSingleOption<FirstType> || IsOptionArray<FirstType>,           \
              "Argument must be OptionMeta or container/array of OptionMeta"); \
          return std::array<cmd::meta::OptionMeta, 0>{};                       \
        }                                                                      \
      } else {                                                                 \
        static_assert((IsSingleOption<Args> && ...),                           \
                      "All arguments must be OptionMeta when multiple "        \
                      "arguments provided");                                   \
        return std::array<cmd::meta::OptionMeta, sizeof...(Args)>{             \
            std::forward<Args>(args)...};                                      \
      }                                                                        \
    }                                                                          \
                                                                               \
    constexpr auto options = []() {                                            \
      return make_option_array_impl(__VA_ARGS__);                              \
    }();                                                                       \
    constexpr size_t option_count = options.size();                            \
    static_assert(option_count > 0, "No options registered!");                 \
    constexpr auto meta = cmd::meta::CommandMeta<option_count>(                \
        std::string_view(cmd_name), std::string_view(cmd_synopsis),            \
        std::string_view(cmd_desc), options, std::string_view(examples),       \
        std::string_view(see_also), std::string_view(author),                  \
        std::string_view(copyright), std::string_view(cmd_synopsis));          \
  }                                                                            \
                                                                               \
  template <size_t N>                                                          \
  int execute##name(CommandContext<N>& ctx) noexcept;                          \
                                                                               \
  namespace {                                                                  \
  struct _Registrar_##name {                                                   \
    _Registrar_##name() {                                                      \
      constexpr size_t N = command_##name##_internal::option_count;            \
      CommandRegistry::registerCommand<N>(                                     \
          cmd_name, command_##name##_internal::meta, execute##name<N>);        \
    }                                                                          \
  };                                                                           \
  _Registrar_##name _registrar_instance_##name;                                \
  }                                                                            \
                                                                               \
  template <size_t N>                                                          \
  int execute##name(CommandContext<N>& ctx) noexcept

#define BOOL_TYPE cmd::meta::OptionType::Bool
#define INT_TYPE cmd::meta::OptionType::Int
#define STRING_TYPE cmd::meta::OptionType::String
#undef OPTION_TYPE
#define OPTION_TYPE(...) OPTION_TYPE_IMPL(__VA_ARGS__, BOOL_TYPE)

#define OPTION_TYPE_IMPL(type, ...) type

#undef BOOL_OPTION
#undef INT_OPTION
#undef STR_OPTION
#undef OPTION

#define BOOL_OPTION(short_name, long_name, description) \
  cmd::meta::OptionMeta { short_name, long_name, description, BOOL_TYPE }

#define INT_OPTION(short_name, long_name, description) \
  cmd::meta::OptionMeta { short_name, long_name, description, INT_TYPE }

#define STR_OPTION(short_name, long_name, description) \
  cmd::meta::OptionMeta { short_name, long_name, description, STRING_TYPE }

#define OPTION(short_name, long_name, description, ...)          \
  cmd::meta::OptionMeta {                                        \
    short_name, long_name, description, OPTION_TYPE(__VA_ARGS__) \
  }

template <typename T>
concept IsOptional =
    requires(T t) {
      typename std::remove_cvref_t<T>::value_type;
      { t.has_value() } -> std::convertible_to<bool>;
      {
        *t
      } -> std::convertible_to<typename std::remove_cvref_t<T>::value_type>;
    } &&
    std::is_same_v<std::remove_cvref_t<T>,
                   std::optional<typename std::remove_cvref_t<T>::value_type>>;

#define DEFINE_OPTION_WRAPPER(wrapper_name, bool_func, arg_func)         \
  template <typename... Args>                                            \
  static __forceinline bool wrapper_name(char opt_char, auto& options,   \
                                         Args&&... args) noexcept {      \
    if constexpr (sizeof...(Args) == 0) {                                \
      return bool_func(opt_char, options);                               \
    } else {                                                             \
      static_assert(sizeof...(Args) == 1,                                \
                    "Only one argument expected for option");            \
      if constexpr (IsOptional<std::decay_t<decltype(args)...>>) {       \
        if (std::get<0>(std::forward_as_tuple(args...)).has_value()) {   \
          return arg_func(                                               \
              opt_char, options,                                         \
              std::get<0>(std::forward_as_tuple(args...)).value());      \
        } else {                                                         \
          return false;                                                  \
        }                                                                \
      } else {                                                           \
        return arg_func(opt_char, options, std::forward<Args>(args)...); \
      }                                                                  \
    }                                                                    \
  }
