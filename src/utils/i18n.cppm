module;

#include <windows.h>

export module utils:i18n;

import std;
import :json;

namespace winux::i18n {

namespace {
std::filesystem::path executable_root() {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                  static_cast<DWORD>(buffer.size()));
  while (size == buffer.size()) {
    buffer.resize(buffer.size() * 2, L'\0');
    size = GetModuleFileNameW(nullptr, buffer.data(),
                              static_cast<DWORD>(buffer.size()));
  }
  buffer.resize(size);
  auto path = std::filesystem::path(buffer);
  return path.has_parent_path() ? path.parent_path()
                                : std::filesystem::current_path();
}

std::string environment_value(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

std::string legacy_key(std::string_view text) {
  // FNV-1a keeps legacy call sites source-compatible while giving extracted
  // literals a stable key independent of their file or line number.
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char ch : text) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return "legacy." + std::format("{:016x}", hash);
}

std::string normalize_locale(std::string locale) {
  for (char& ch : locale) {
    if (ch == '_') ch = '-';
  }
  return locale;
}

std::optional<nlohmann::json> read_catalog(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return std::nullopt;
  std::string text{std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>{}};
  try {
    auto catalog = nlohmann::json::parse(text);
    if (!catalog.is_object() ||
        !catalog.value("messages", nlohmann::json::object()).is_object()) {
      return std::nullopt;
    }
    return catalog;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

struct Catalog {
  bool enabled = false;
  std::string locale;
  nlohmann::json messages = nlohmann::json::object();
};

const Catalog& catalog() {
  static const Catalog loaded = [] {
    Catalog result;
    result.locale = normalize_locale(environment_value("WINUX_LANG"));
    if (result.locale.empty() || result.locale == "off" ||
        result.locale == "none" || result.locale == "C") {
      result.locale.clear();
      return result;
    }
    result.enabled = true;

    auto root = executable_root() / ".wpm" / "i18n";
    auto path = root / result.locale / "catalog.json";
    auto parsed = read_catalog(path);
    if (!parsed && result.locale.find('-') != std::string::npos) {
      auto language = result.locale.substr(0, result.locale.find('-'));
      parsed = read_catalog(root / language / "catalog.json");
    }
    if (parsed) result.messages = std::move(*parsed)["messages"];
    return result;
  }();
  return loaded;
}
}  // namespace

export std::string locale() { return catalog().locale; }

export std::string translate(std::string_view key, std::string_view fallback) {
  const auto& messages = catalog().messages;
  auto it = messages.find(std::string(key));
  if (it == messages.end() || !it->is_string()) return std::string(fallback);
  return it->get<std::string>();
}

export template <typename... Args>
std::string format(std::string_view key, std::string_view fallback,
                   Args&&... args) {
  const auto message = translate(key, fallback);
  if constexpr (sizeof...(Args) == 0) {
    return message;
  } else {
    try {
      return std::vformat(message, std::make_format_args(args...));
    } catch (const std::format_error&) {
      // A malformed external catalog entry must never terminate a command.
      return std::vformat(fallback, std::make_format_args(args...));
    }
  }
}

export std::string translate_legacy(std::string_view text);

export std::string translate_error(std::string_view error) {
  if (error == "missing operand") {
    return translate("common.error.missing_operand", error);
  }
  // GNU appends strerror(EOVERFLOW) after the quoted value; translate the
  // quoted part and keep the suffix translatable.
  constexpr std::string_view kOverflowSuffix =
      ": Value too large for defined data type";
  if (error.size() > kOverflowSuffix.size() &&
      error.ends_with(kOverflowSuffix)) {
    return translate_error(
               error.substr(0, error.size() - kOverflowSuffix.size())) +
           translate("common.error.value_too_large", kOverflowSuffix);
  }
  // "invalid repeat count '<digits>' in [c*n] construct" (tr).
  constexpr std::string_view kRepeatCountPrefix = "invalid repeat count '";
  constexpr std::string_view kRepeatCountSuffix = "' in [c*n] construct";
  if (error.size() > kRepeatCountPrefix.size() + kRepeatCountSuffix.size() &&
      error.starts_with(kRepeatCountPrefix) &&
      error.ends_with(kRepeatCountSuffix)) {
    const auto value = error.substr(
        kRepeatCountPrefix.size(),
        error.size() - kRepeatCountPrefix.size() - kRepeatCountSuffix.size());
    return ::winux::i18n::format("command.tr.error.invalid_repeat_count",
                                 "invalid repeat count '{}' in [c*n] construct",
                                 value);
  }
  // Static tr [c*] repeat-construct diagnostics.
  if (error == "the [c*] repeat construct may not appear in string1") {
    return translate("command.tr.error.string1_repeat", error);
  }
  if (error == "only one [c*] repeat construct may appear in string2") {
    return translate("command.tr.error.one_repeat_string2", error);
  }
  if (error ==
      "the [c*] construct may appear in string2 only when "
      "translating") {
    return translate("command.tr.error.repeat_only_translate", error);
  }
  // "'<token>': unary operator expected" — the value precedes the quote.
  constexpr std::string_view kUnarySuffix = "': unary operator expected";
  if (error.size() > kUnarySuffix.size() && error.starts_with('\'') &&
      error.ends_with(kUnarySuffix)) {
    const auto value = error.substr(1, error.size() - 1 - kUnarySuffix.size());
    return ::winux::i18n::format("common.error.unary_operator",
                                 "'{}': unary operator expected", value);
  }
  // "'<pattern>': line number out of range" (csplit) — the pattern precedes
  // the quote-suffix.
  constexpr std::string_view kLineRangeSuffix = "': line number out of range";
  if (error.size() > kLineRangeSuffix.size() && error.starts_with('\'') &&
      error.ends_with(kLineRangeSuffix)) {
    const auto value =
        error.substr(1, error.size() - 1 - kLineRangeSuffix.size());
    return ::winux::i18n::format("command.csplit.error.line_out_of_range",
                                 "'{}': line number out of range", value);
  }
  // "'<pattern>': match not found" (csplit).
  constexpr std::string_view kMatchNotFoundSuffix = "': match not found";
  if (error.size() > kMatchNotFoundSuffix.size() && error.starts_with('\'') &&
      error.ends_with(kMatchNotFoundSuffix)) {
    const auto value =
        error.substr(1, error.size() - 1 - kMatchNotFoundSuffix.size());
    return ::winux::i18n::format("command.csplit.error.match_not_found",
                                 "'{}': match not found", value);
  }
  // "'<spec>': invalid signal" / "'<spec>': multiple signals specified"
  // (kill; [GNU] operand2sig wording, uutils #14177) — the spec precedes
  // the quote-suffix.
  constexpr std::string_view kInvalidSignalSuffix = "': invalid signal";
  if (error.size() > kInvalidSignalSuffix.size() && error.starts_with('\'') &&
      error.ends_with(kInvalidSignalSuffix)) {
    const auto value =
        error.substr(1, error.size() - 1 - kInvalidSignalSuffix.size());
    return ::winux::i18n::format("command.kill.error.invalid_signal",
                                 "'{}': invalid signal", value);
  }
  constexpr std::string_view kMultipleSignalsSuffix =
      "': multiple signals specified";
  if (error.size() > kMultipleSignalsSuffix.size() && error.starts_with('\'') &&
      error.ends_with(kMultipleSignalsSuffix)) {
    const auto value =
        error.substr(1, error.size() - 1 - kMultipleSignalsSuffix.size());
    return ::winux::i18n::format("command.kill.error.multiple_signals",
                                 "'{}': multiple signals specified", value);
  }
  // sort: "options '-cC' are incompatible" — the flag cluster is quoted in
  // the middle of the message.
  constexpr std::string_view kIncompatibleSuffix = "' are incompatible";
  if (error.starts_with("options '") && error.ends_with(kIncompatibleSuffix)) {
    const auto value =
        error.substr(9, error.size() - 9 - kIncompatibleSuffix.size());
    return ::winux::i18n::format("common.error.options_incompatible",
                                 "options '{}' are incompatible", value);
  }
  // pr: "'-l' invalid number of lines: '0'" — three dynamic parts (option
  // label, unit, value).
  {
    constexpr std::string_view kPrNumberSep = "' invalid number of ";
    if (error.size() > kPrNumberSep.size() + 2 && error.front() == '\'' &&
        error.back() == '\'') {
      const auto sep_pos = error.find(kPrNumberSep, 1);
      if (sep_pos != std::string_view::npos) {
        const auto label = error.substr(1, sep_pos - 1);
        const auto rest = error.substr(sep_pos + kPrNumberSep.size());
        constexpr std::string_view kPrValueSep = ": '";
        const auto value_sep = rest.rfind(kPrValueSep);
        if (value_sep != std::string_view::npos && value_sep > 0) {
          const auto what = rest.substr(0, value_sep);
          auto value = rest.substr(value_sep + kPrValueSep.size());
          value.remove_suffix(1);
          return ::winux::i18n::format("command.pr.error.invalid_number",
                                       "'{}' invalid number of {}: '{}'", label,
                                       what, value);
        }
      }
    }
  }
  // pr: "'-e' extra characters or invalid number in the argument: '0'".
  {
    constexpr std::string_view kPrExtraSep =
        "' extra characters or invalid number in the argument: '";
    if (error.size() > kPrExtraSep.size() && error.front() == '\'' &&
        error.back() == '\'') {
      const auto sep_pos = error.find(kPrExtraSep);
      if (sep_pos != std::string_view::npos && sep_pos > 0) {
        const auto label = error.substr(1, sep_pos - 1);
        auto value = error.substr(sep_pos + kPrExtraSep.size());
        value.remove_suffix(1);
        return ::winux::i18n::format(
            "command.pr.error.extra_characters",
            "'{}' extra characters or invalid number in the argument: '{}'",
            label, value);
      }
    }
  }
  constexpr std::array<std::pair<std::string_view, std::string_view>, 32> exact{
      {{"invalid input", "common.error.invalid_input"},
       {"error reading from file", "common.error.read_file"},
       {"error reading input", "common.error.read_input"},
       {"error reading from standard input", "common.error.read_stdin"},
       {"missing file operand", "common.error.missing_file"},
       {"invalid block size", "common.error.invalid_block_size"},
       {"invalid length", "common.error.invalid_length"},
       {"invalid range", "common.error.invalid_range"},
       {"invalid input range", "common.error.invalid_input_range"},
       {"invalid wrap size", "common.error.invalid_wrap"},
       {"invalid line count", "common.error.invalid_line_count"},
       {"Numerical result out of range", "common.error.numerical_range"},
       {"invalid regular expression", "common.error.invalid_regex"},
       {"target is not a directory", "common.error.target_directory"},
       {"cannot create directory", "common.error.create_directory"},
       {"cannot open for reading", "common.error.open_read"},
       {"cannot open for writing", "common.error.open_write"},
       {"cannot read source metadata", "common.error.read_metadata"},
       {"cannot write destination metadata", "common.error.write_metadata"},
       {"cannot preserve timestamps", "common.error.preserve_timestamps"},
       {"cannot preserve attributes", "common.error.preserve_attributes"},
       {"cannot create backup for destination", "common.error.create_backup"},
       {"source and destination are the same file", "common.error.same_file"},
       {"failed to hash data", "common.error.hash_data"},
       {"failed to acquire cryptographic context",
        "common.error.crypto_context"},
       {"failed to create hash object", "common.error.hash_object"},
       {"failed to get hash value", "common.error.hash_value"},
       {"tab size cannot be 0", "common.error.tab_zero"},
       {"No such file or directory", "common.error.no_such_file"},
       {"you must specify either '--size' or '--reference'",
        "common.error.size_or_reference"},
       {"cannot both summarize and show all",
        "common.error.summarize_and_show_all"},
       {"multiple output files specified",
        "common.error.multiple_output_files"}}};
  for (const auto [literal, key] : exact) {
    if (error == literal) return translate(key, error);
  }
  constexpr std::array<std::pair<std::string_view, std::string_view>, 6>
      patterns{{{"missing operand after '", "common.error.missing_after"},
                {"missing argument after '", "common.error.missing_arg_after"},
                {"invalid integer '", "common.error.invalid_integer"},
                {"extra operand '", "common.error.extra_operand"},
                {"invalid argument '", "common.error.invalid_argument"},
                {"error reading '", "common.error.reading"}}};
  for (const auto [prefix, key] : patterns) {
    if (!error.starts_with(prefix) || error.back() != '\'') continue;
    const auto value =
        error.substr(prefix.size(), error.size() - prefix.size() - 1);
    if (key == "common.error.missing_after") {
      return ::winux::i18n::format(key, "missing operand after '{}'", value);
    }
    if (key == "common.error.missing_arg_after") {
      return ::winux::i18n::format(key, "missing argument after '{}'", value);
    }
    if (key == "common.error.invalid_integer") {
      return ::winux::i18n::format(key, "invalid integer '{}'", value);
    }
    if (key == "common.error.extra_operand") {
      return ::winux::i18n::format(key, "extra operand '{}'", value);
    }
    if (key == "common.error.invalid_argument") {
      return ::winux::i18n::format(key, "invalid argument '{}'", value);
    }
    return ::winux::i18n::format(key, "error reading '{}'", value);
  }
  constexpr std::array<std::pair<std::string_view, std::string_view>, 41>
      quoted{
          {{"cannot open '", "common.error.cannot_open"},
           {"cannot access '", "common.error.cannot_access"},
           {"cannot stat '", "common.error.cannot_stat"},
           {"cannot create '", "common.error.cannot_create"},
           {"error writing '", "common.error.write"},
           {"invalid mode: '", "common.error.invalid_mode"},
           {"invalid group: '", "common.error.invalid_group"},
           {"invalid user: '", "common.error.invalid_user"},
           {"invalid spec: '", "common.error.invalid_spec"},
           {"invalid encoding '", "common.error.invalid_encoding"},
           {"invalid type '", "common.error.invalid_type"},
           {"invalid device type '", "common.error.invalid_device_type"},
           {"invalid suffix '", "common.error.invalid_suffix"},
           {"invalid time interval '", "common.error.invalid_time_interval"},
           {"invalid character class '", "common.error.invalid_char_class"},
           {"unknown registry root '", "common.error.unknown_registry_root"},
           {"invalid input range: '", "common.error.invalid_input_range"},
           {"invalid line count: '", "common.error.invalid_line_count_value"},
           {"invalid wrap size: '", "common.error.invalid_wrap_value"},
           {"invalid number of bytes: '", "common.error.invalid_num_bytes"},
           {"invalid number of columns: '", "common.error.invalid_num_columns"},
           {"invalid number of lines: '", "common.error.invalid_num_lines"},
           {"invalid number of chunks: '", "common.error.invalid_num_chunks"},
           {"invalid chunk number: '", "common.error.invalid_chunk_number"},
           {"tab size contains invalid character(s): '",
            "common.error.tab_invalid_chars"},
           {"tab stop is too large '", "common.error.tab_too_large"},
           {"cannot copy '", "common.error.cannot_copy"},
           {"cannot delete source file '", "common.error.cannot_delete_file"},
           {"cannot copy directory '", "common.error.cannot_copy_directory"},
           {"cannot delete source directory '",
            "common.error.cannot_delete_directory"},
           {"failed to create hard link '", "common.error.create_hardlink"},
           {"failed to access '", "common.error.failed_access"},
           {"failed to create symbolic link '", "common.error.create_symlink"},
           {"failed to remove '", "common.error.failed_remove"},
           {"cannot open script file '", "common.error.open_script"},
           {"invalid gap width: '", "common.error.invalid_gap_width"},
           {"invalid --parallel argument '",
            "common.error.invalid_parallel_argument"},
           {"Invalid number: '", "common.error.invalid_number"},
           {"invalid field number: '", "common.error.invalid_field_number"},
           {"invalid tab size: '", "common.error.invalid_tab_size"},
           {"too few X's in template '", "common.error.too_few_xs"}}};
  for (const auto [prefix, key] : quoted) {
    if (!error.starts_with(prefix) || error.back() != '\'') continue;
    const auto value =
        error.substr(prefix.size(), error.size() - prefix.size() - 1);
    if (key == "common.error.cannot_open")
      return ::winux::i18n::format(key, "cannot open '{}'", value);
    if (key == "common.error.cannot_access")
      return ::winux::i18n::format(key, "cannot access '{}'", value);
    if (key == "common.error.cannot_stat")
      return ::winux::i18n::format(key, "cannot stat '{}'", value);
    if (key == "common.error.cannot_create")
      return ::winux::i18n::format(key, "cannot create '{}'", value);
    if (key == "common.error.write")
      return ::winux::i18n::format(key, "error writing '{}'", value);
    if (key == "common.error.invalid_mode")
      return ::winux::i18n::format(key, "invalid mode: '{}'", value);
    constexpr std::array<std::pair<std::string_view, std::string_view>, 35>
        fallbacks{
            {{"common.error.invalid_group", "invalid group: '{}'"},
             {"common.error.invalid_user", "invalid user: '{}'"},
             {"common.error.invalid_spec", "invalid spec: '{}'"},
             {"common.error.invalid_encoding", "invalid encoding '{}'"},
             {"common.error.invalid_type", "invalid type '{}'"},
             {"common.error.invalid_device_type", "invalid device type '{}'"},
             {"common.error.invalid_suffix", "invalid suffix '{}'"},
             {"common.error.invalid_time_interval",
              "invalid time interval '{}'"},
             {"common.error.invalid_char_class",
              "invalid character class '{}'"},
             {"common.error.unknown_registry_root",
              "unknown registry root '{}'"},
             {"common.error.invalid_input_range", "invalid input range: '{}'"},
             {"common.error.invalid_line_count_value",
              "invalid line count: '{}'"},
             {"common.error.invalid_wrap_value", "invalid wrap size: '{}'"},
             {"common.error.invalid_num_bytes",
              "invalid number of bytes: '{}'"},
             {"common.error.invalid_num_columns",
              "invalid number of columns: '{}'"},
             {"common.error.invalid_num_lines",
              "invalid number of lines: '{}'"},
             {"common.error.invalid_num_chunks",
              "invalid number of chunks: '{}'"},
             {"common.error.invalid_chunk_number",
              "invalid chunk number: '{}'"},
             {"common.error.tab_invalid_chars",
              "tab size contains invalid character(s): '{}'"},
             {"common.error.tab_too_large", "tab stop is too large '{}'"},
             {"common.error.cannot_copy", "cannot copy '{}'"},
             {"common.error.cannot_delete_file",
              "cannot delete source file '{}'"},
             {"common.error.cannot_copy_directory",
              "cannot copy directory '{}'"},
             {"common.error.cannot_delete_directory",
              "cannot delete source directory '{}'"},
             {"common.error.create_hardlink",
              "failed to create hard link '{}'"},
             {"common.error.failed_access", "failed to access '{}'"},
             {"common.error.create_symlink",
              "failed to create symbolic link '{}'"},
             {"common.error.failed_remove", "failed to remove '{}'"},
             {"common.error.open_script", "cannot open script file '{}'"},
             {"common.error.invalid_gap_width", "invalid gap width: '{}'"},
             {"common.error.invalid_parallel_argument",
              "invalid --parallel argument '{}'"},
             {"common.error.invalid_number", "Invalid number: '{}'"},
             {"common.error.invalid_field_number",
              "invalid field number: '{}'"},
             {"common.error.invalid_tab_size", "invalid tab size: '{}'"},
             {"common.error.too_few_xs", "too few X's in template '{}'"}}};
    for (const auto [fallback_key, fallback] : fallbacks) {
      if (key == fallback_key)
        return ::winux::i18n::format(key, fallback, value);
    }
    return translate_legacy(error);
  }
  return translate_legacy(error);
}

export std::string translate_help_hint(std::string_view text) {
  constexpr std::string_view prefix = "Try '";
  constexpr std::string_view suffix = " --help for more information.";
  if (!text.starts_with(prefix) || !text.ends_with(suffix)) {
    return std::string(text);
  }
  const auto command =
      text.substr(prefix.size(), text.size() - prefix.size() - suffix.size());
  if (command.empty() ||
      command.find_first_of("' \t\r\n") != std::string_view::npos) {
    return std::string(text);
  }
  return format("common.try_help", "Try '{} --help' for more information.",
                command);
}

export std::string translate_legacy(std::string_view text) {
  return translate(legacy_key(text), text);
}

export bool has_catalog() { return !catalog().messages.empty(); }

}  // namespace winux::i18n
