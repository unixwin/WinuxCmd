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
 *  - File: command_context.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
export module core:command_context;
import :opt;

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
    const std::array<cmd::meta::OptionMeta, N>& metas, bool& ok) {
  auto parsed = parse_command(args, metas);
  ok = parsed.ok;

  CommandContext<N> ctx;
  ctx.metas = &metas;
  ctx.options = std::move(parsed.options);
  ctx.raw_args.assign(args.begin(), args.end());
  ctx.positionals = std::move(parsed.positionals);

  return ctx;
}
