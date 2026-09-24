// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for df - display disk space usage
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief DF command options definition
 *
 * This array defines all the options supported by the df command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -i, @a --inodes: list inode information instead of block usage
 * [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -T, @a --print-type: print file system type [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [IMPLEMENTED]
 * - @a --total: produce a grand total [IMPLEMENTED]
 * - @a --output[=FIELD_LIST]: use GNU-style output fields [IMPLEMENTED]
 * - @a -a, @a --all: include all file systems [ACCEPTED]
 * - @a --sync, @a --no-sync: sync control [ACCEPTED]
 */
auto constexpr DF_OPTIONS = std::array{
    OPTION("-a", "--all", "include all file systems"),
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-i", "--inodes", "list inode information instead of block usage"),
    OPTION("-k", "", "like --block-size=1K"),
    // [GNU] -m is a synonym for --block-size=1M (uutils #11565)
    OPTION("-m", "", "like --block-size=1M"),
    OPTION("-l", "--local", "limit listing to local file systems"),
    OPTION("-T", "--print-type", "print file system type"),
    OPTION("-t", "--type", "limit listing to file systems of type TYPE",
           STRING_TYPE),
    // [GNU] -F: obsolete Solaris synonym for -t/--type; hidden
    OPTION("-F", "", "", STRING_TYPE),
    OPTION("-x", "--exclude-type",
           "limit listing to file systems not of type TYPE", STRING_TYPE),
    OPTION("", "--total", "produce a grand total"),
    OPTION("", "--sync", "invoke sync before getting usage info"),
    OPTION("", "--no-sync", "do not invoke sync before getting usage info"),
    OPTION("", "--output", "use the output format defined by FIELD_LIST",
           OPTIONAL_STRING_TYPE),
    OPTION("-P", "--portability", "use the POSIX output format"),
    // [GNU]
    // [DIFFERS] GNU: -v is "(ignored)"; WinuxCmd: was version display
    OPTION("-v", "", "(ignored) [DIFFERS: GNU ignores -v]")};

// ======================================================
// Pipeline components
// ======================================================
namespace df_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Format size to human-readable string
 *
 * [GNU] Mirrors coreutils human_readable(): values round UP (ceiling) and
 * roll over to the next unit when the ceiling reaches the base (Savannah
 * #1154).
 *
 * @param size Size in bytes
 * @param si Use 1000-based units instead of 1024-based
 * @return Formatted string
 */
auto format_size(uint64_t size, bool si) -> std::string {
  const char* units = si ? "BKMGTPE"  // 1000-based
                         : "BKMGTP";  // 1024-based
  const uint64_t base = si ? 1000 : 1024;

  if (size < base) {
    return std::to_string(size);
  }

  // Smallest unit index (1 = K) with size in [base^idx, base^(idx+1)).
  int idx = 0;
  uint64_t unit = base;
  while (idx < 6 && size / unit >= base) {
    unit *= base;
    ++idx;
  }
  ++idx;

  // Ceiling in tenths of the selected unit (exact integer math; the
  // remainder-based form avoids overflow for exabyte-scale sizes).
  const uint64_t q = size / unit;
  const uint64_t r = size % unit;
  uint64_t tenths = q * 10 + (r * 10 + unit - 1) / unit;

  // Rollover: 1024.0K displays as 1.0M.
  while (tenths >= base * 10 && idx < 6) {
    tenths = (tenths + base - 1) / base;
    ++idx;
  }

  char buf[32];
  if (tenths < 100) {  // value < 10 units: one decimal digit
    snprintf(buf, sizeof(buf), "%llu.%llu%c",
             static_cast<unsigned long long>(tenths / 10),
             static_cast<unsigned long long>(tenths % 10), units[idx]);
  } else {
    snprintf(buf, sizeof(buf), "%llu%c",
             static_cast<unsigned long long>((tenths + 9) / 10), units[idx]);
  }
  return std::string(buf);
}

auto pow_u64(uint64_t base, int exponent) -> std::optional<uint64_t> {
  uint64_t value = 1;
  for (int i = 0; i < exponent; ++i) {
    if (value > std::numeric_limits<uint64_t>::max() / base) {
      return std::nullopt;
    }
    value *= base;
  }
  return value;
}

// Parses a GNU-style block size spec. When |display_suffix| is non-null and
// the spec is a bare suffix without a leading integer (e.g. "M"), the suffix
// text is stored there: GNU appends it to output sizes in that case.
auto parse_block_size(std::string_view text,
                      std::string* display_suffix = nullptr)
    -> std::optional<uint64_t> {
  if (text.empty()) {
    return std::nullopt;
  }

  struct Unit {
    std::string_view suffix;
    uint64_t multiplier;
  };

  const std::array units{Unit{"KiB", *pow_u64(1024, 1)},
                         Unit{"MiB", *pow_u64(1024, 2)},
                         Unit{"GiB", *pow_u64(1024, 3)},
                         Unit{"TiB", *pow_u64(1024, 4)},
                         Unit{"PiB", *pow_u64(1024, 5)},
                         Unit{"EiB", *pow_u64(1024, 6)},
                         Unit{"KB", *pow_u64(1000, 1)},
                         Unit{"MB", *pow_u64(1000, 2)},
                         Unit{"GB", *pow_u64(1000, 3)},
                         Unit{"TB", *pow_u64(1000, 4)},
                         Unit{"PB", *pow_u64(1000, 5)},
                         Unit{"EB", *pow_u64(1000, 6)},
                         Unit{"kiB", *pow_u64(1024, 1)},
                         Unit{"miB", *pow_u64(1024, 2)},
                         Unit{"giB", *pow_u64(1024, 3)},
                         Unit{"tiB", *pow_u64(1024, 4)},
                         Unit{"piB", *pow_u64(1024, 5)},
                         Unit{"eiB", *pow_u64(1024, 6)},
                         Unit{"kb", *pow_u64(1000, 1)},
                         Unit{"mb", *pow_u64(1000, 2)},
                         Unit{"gb", *pow_u64(1000, 3)},
                         Unit{"tb", *pow_u64(1000, 4)},
                         Unit{"pb", *pow_u64(1000, 5)},
                         Unit{"eb", *pow_u64(1000, 6)},
                         Unit{"K", *pow_u64(1024, 1)},
                         Unit{"M", *pow_u64(1024, 2)},
                         Unit{"G", *pow_u64(1024, 3)},
                         Unit{"T", *pow_u64(1024, 4)},
                         Unit{"P", *pow_u64(1024, 5)},
                         Unit{"E", *pow_u64(1024, 6)},
                         Unit{"k", *pow_u64(1024, 1)},
                         Unit{"m", *pow_u64(1024, 2)},
                         Unit{"g", *pow_u64(1024, 3)},
                         Unit{"t", *pow_u64(1024, 4)},
                         Unit{"p", *pow_u64(1024, 5)},
                         Unit{"e", *pow_u64(1024, 6)},
                         Unit{"b", 512},
                         Unit{"B", 1}};

  uint64_t multiplier = 1;
  std::string_view number = text;
  bool suffix_matched = false;
  std::string_view matched_suffix;
  for (const auto& unit : units) {
    if (text.size() >= unit.suffix.size() &&
        text.substr(text.size() - unit.suffix.size()) == unit.suffix) {
      multiplier = unit.multiplier;
      number = text.substr(0, text.size() - unit.suffix.size());
      suffix_matched = true;
      matched_suffix = unit.suffix;
      break;
    }
  }

  uint64_t value = 1;
  if (!number.empty()) {
    auto [ptr, ec] =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc() || ptr != number.data() + number.size()) {
      return std::nullopt;
    }
  } else if (!suffix_matched) {
    return std::nullopt;
  }

  if (value == 0 || value > std::numeric_limits<uint64_t>::max() / multiplier) {
    return std::nullopt;
  }
  if (display_suffix != nullptr) {
    *display_suffix = (suffix_matched && number.empty())
                          ? std::string(matched_suffix)
                          : std::string();
  }
  return value * multiplier;
}

auto ceil_div(uint64_t value, uint64_t divisor) -> uint64_t {
  if (value == 0) {
    return 0;
  }
  return 1 + ((value - 1) / divisor);
}

struct OutputConfig {
  bool human = false;
  bool si = false;
  bool portability = false;
  bool block_size_explicit = false;
  // [GNU] Default block size is 1K (512 only under POSIX -P), matching
  // coreutils df.
  uint64_t block_size = 1024;
  std::string block_label = "1K-blocks";
  // Suffix appended to scaled output when --block-size used a bare unit
  // suffix (e.g. "df -BM" prints sizes as "1M").
  std::string display_suffix;
};

enum class OutputField {
  Source,
  FsType,
  Size,
  Used,
  Avail,
  Pcent,
  ITotal,
  IUsed,
  IAvail,
  IPcent,
  Target,
  File,
};

struct OutputColumn {
  OutputField field;
  std::string header;
  bool align_right = false;
};

auto make_output_error(std::string message) -> std::unexpected<std::string> {
  return std::unexpected(std::move(message));
}

auto parse_output_field(std::string_view name) -> std::optional<OutputField> {
  if (name == "source") return OutputField::Source;
  if (name == "fstype") return OutputField::FsType;
  if (name == "size") return OutputField::Size;
  if (name == "used") return OutputField::Used;
  if (name == "avail") return OutputField::Avail;
  if (name == "pcent") return OutputField::Pcent;
  if (name == "itotal") return OutputField::ITotal;
  if (name == "iused") return OutputField::IUsed;
  if (name == "iavail") return OutputField::IAvail;
  if (name == "ipcent") return OutputField::IPcent;
  if (name == "target") return OutputField::Target;
  if (name == "file") return OutputField::File;
  return std::nullopt;
}

auto output_field_name(OutputField field) -> std::string_view {
  switch (field) {
    case OutputField::Source:
      return "source";
    case OutputField::FsType:
      return "fstype";
    case OutputField::Size:
      return "size";
    case OutputField::Used:
      return "used";
    case OutputField::Avail:
      return "avail";
    case OutputField::Pcent:
      return "pcent";
    case OutputField::ITotal:
      return "itotal";
    case OutputField::IUsed:
      return "iused";
    case OutputField::IAvail:
      return "iavail";
    case OutputField::IPcent:
      return "ipcent";
    case OutputField::Target:
      return "target";
    case OutputField::File:
      return "file";
  }
  return "";
}

auto output_field_header(OutputField field, const OutputConfig& output)
    -> std::string {
  switch (field) {
    case OutputField::Source:
      return "Filesystem";
    case OutputField::FsType:
      return "Type";
    case OutputField::Size:
      return (output.human || output.si) ? "Size" : output.block_label;
    case OutputField::Used:
      return "Used";
    case OutputField::Avail:
      return "Avail";
    case OutputField::Pcent:
      return "Use%";
    case OutputField::ITotal:
      return "Inodes";
    case OutputField::IUsed:
      return "IUsed";
    case OutputField::IAvail:
      return "IFree";
    case OutputField::IPcent:
      return "IUse%";
    case OutputField::Target:
      return "Mounted on";
    case OutputField::File:
      return "File";
  }
  return "";
}

auto output_field_align_right(OutputField field) -> bool {
  switch (field) {
    case OutputField::Size:
    case OutputField::Used:
    case OutputField::Avail:
    case OutputField::Pcent:
    case OutputField::ITotal:
    case OutputField::IUsed:
    case OutputField::IAvail:
    case OutputField::IPcent:
      return true;
    default:
      return false;
  }
}

auto append_output_fields(std::vector<OutputField>& fields,
                          std::string_view csv) -> cp::Result<bool> {
  size_t start = 0;
  while (start <= csv.size()) {
    size_t comma = csv.find(',', start);
    std::string_view token = comma == std::string_view::npos
                                 ? csv.substr(start)
                                 : csv.substr(start, comma - start);

    auto field = parse_output_field(token);
    if (!field) {
      return make_output_error("option --output: field " + std::string(token) +
                               " unknown");
    }
    if (std::find(fields.begin(), fields.end(), *field) != fields.end()) {
      return make_output_error("option --output: field " +
                               std::string(output_field_name(*field)) +
                               " used more than once");
    }
    fields.push_back(*field);

    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return true;
}

template <size_t N>
auto has_empty_inline_output_value(const CommandContext<N>& ctx) -> bool {
  for (std::string_view arg : ctx.raw_args) {
    if (arg == "--output=") {
      return true;
    }
  }
  return false;
}

template <size_t N>
auto parse_output_columns(const CommandContext<N>& ctx,
                          const OutputConfig& output)
    -> cp::Result<std::vector<OutputColumn>> {
  std::vector<OutputField> fields;
  constexpr std::string_view all_fields =
      "source,fstype,itotal,iused,iavail,ipcent,size,used,avail,pcent,file,"
      "target";

  if (has_empty_inline_output_value(ctx)) {
    return make_output_error("option --output: field  unknown");
  }

  for (const auto& occurrence : ctx.string_occurrences({"--output"})) {
    std::string_view list =
        occurrence.value.empty() ? all_fields : occurrence.value;
    auto appended = append_output_fields(fields, list);
    if (!appended) {
      return std::unexpected(appended.error());
    }
  }

  std::vector<OutputColumn> columns;
  columns.reserve(fields.size());
  for (auto field : fields) {
    columns.push_back(
        OutputColumn{.field = field,
                     .header = output_field_header(field, output),
                     .align_right = output_field_align_right(field)});
  }
  return columns;
}

auto block_label_for(std::string_view value) -> std::string {
  if (value.empty()) {
    return "blocks";
  }

  bool all_digits = true;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      all_digits = false;
      break;
    }
  }

  std::string label;
  if (all_digits) {
    label.assign(value);
    label += "B";
  } else if (value.front() < '0' || value.front() > '9') {
    label = "1";
    label.append(value.data(), value.size());
  } else {
    label.assign(value);
  }
  label += "-blocks";
  return label;
}

// [GNU] Environment-specified block sizes get a computed header label:
// 1024 -> "1K-blocks", 4096 -> "4K-blocks", 512 -> "512B-blocks".
auto block_label_for_size(uint64_t block_size) -> std::string {
  static constexpr std::string_view kUnits = "KMGTPE";
  for (int power = static_cast<int>(kUnits.size()); power >= 1; --power) {
    const uint64_t scale = *pow_u64(1024, power);
    if (block_size % scale == 0) {
      return std::to_string(block_size / scale) +
             std::string(1, kUnits[static_cast<size_t>(power - 1)]) + "-blocks";
    }
  }
  return std::to_string(block_size) + "B-blocks";
}

/**
 * @brief Get disk free space information
 * @param path Path to check (any file/directory on the volume)
 * @return Disk info or error message
 */
struct DiskInfo {
  std::string filesystem;
  std::string type;
  std::string mount_point;
  uint64_t total = 0;
  uint64_t total_free = 0;
  uint64_t available = 0;
};

auto get_disk_info(const std::string& path) -> std::optional<DiskInfo> {
  std::wstring wpath = utf8_to_wstring(path);
  std::vector<wchar_t> full_path(MAX_PATH);
  DWORD full_len =
      GetFullPathNameW(wpath.c_str(), static_cast<DWORD>(full_path.size()),
                       full_path.data(), nullptr);
  if (full_len == 0) {
    return std::nullopt;
  }
  if (full_len >= full_path.size()) {
    full_path.resize(full_len + 1);
    full_len =
        GetFullPathNameW(wpath.c_str(), static_cast<DWORD>(full_path.size()),
                         full_path.data(), nullptr);
    if (full_len == 0 || full_len >= full_path.size()) {
      return std::nullopt;
    }
  }

  std::vector<wchar_t> volume_path(MAX_PATH);
  if (!GetVolumePathNameW(full_path.data(), volume_path.data(),
                          static_cast<DWORD>(volume_path.size()))) {
    return std::nullopt;
  }

  ULARGE_INTEGER free_bytes, total_bytes, total_free;

  if (!GetDiskFreeSpaceExW(volume_path.data(), &free_bytes, &total_bytes,
                           &total_free)) {
    return std::nullopt;
  }

  // Get volume information
  wchar_t volume_name[MAX_PATH];
  wchar_t file_system_name[MAX_PATH] = {0};
  std::string fs_type = "unknown";

  if (GetVolumeInformationW(volume_path.data(), volume_name, MAX_PATH, nullptr,
                            nullptr, nullptr, file_system_name, MAX_PATH)) {
    fs_type = wstring_to_utf8(file_system_name);
  }

  return DiskInfo{.filesystem = wstring_to_utf8(volume_path.data()),
                  .type = fs_type,
                  .mount_point = wstring_to_utf8(volume_path.data()),
                  .total = total_bytes.QuadPart,
                  .total_free = total_free.QuadPart,
                  .available = free_bytes.QuadPart};
}

auto print_usage_header(const OutputConfig& output, bool print_type,
                        bool inodes) -> void {
  safePrint("Filesystem     ");
  if (print_type) {
    safePrint("Type       ");
  }

  if (inodes) {
    safePrintLn("      Inodes      IUsed      IFree IUse% Mounted on");
    return;
  }

  // [GNU] POSIX -P mode uses the historical "Capacity" header.
  const char* use_label = output.portability ? "Capacity" : "Use%";
  if (output.human || output.si) {
    // [GNU] Header words match coreutils df: "Avail"/"Use%" in human mode
    // (Savannah #14358).
    safePrint("     Size   Used     Avail Use%");
  } else {
    char buf[128];
    // [GNU] Block mode keeps "Available" but uses "Use%" (Savannah #14358).
    snprintf(buf, sizeof(buf), "%16s        Used    Available %s",
             output.block_label.c_str(), use_label);
    safePrint(buf);
  }
  safePrintLn(L" Mounted on");
}

auto print_size_columns(uint64_t total, uint64_t used, uint64_t available,
                        const OutputConfig& output) -> void {
  if (output.human || output.si) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%9s %6s %10s ",
             format_size(total, output.si).c_str(),
             format_size(used, output.si).c_str(),
             format_size(available, output.si).c_str());
    safePrint(buf);
  } else {
    char buf[160];
    snprintf(buf, sizeof(buf), "%16ju%s %12ju%s %12ju%s ",
             static_cast<uintmax_t>(ceil_div(total, output.block_size)),
             output.display_suffix.c_str(),
             static_cast<uintmax_t>(ceil_div(used, output.block_size)),
             output.display_suffix.c_str(),
             static_cast<uintmax_t>(ceil_div(available, output.block_size)),
             output.display_suffix.c_str());
    safePrint(buf);
  }
}

auto configure_output(const CommandContext<DF_OPTIONS.size()>& ctx)
    -> cp::Result<OutputConfig> {
  OutputConfig output;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= DF_OPTIONS.size()) {
      continue;
    }

    const auto& meta = DF_OPTIONS[occurrence.index];
    if (meta.short_name == "-k") {
      output.human = false;
      output.si = false;
      output.block_size = 1024;
      output.block_label = "1K-blocks";
      output.block_size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-m") {
      output.human = false;
      output.si = false;
      output.block_size = 1024 * 1024;
      output.block_label = "1M-blocks";
      output.block_size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-h" || meta.long_name == "--human-readable") {
      output.human = true;
      output.si = false;
      output.block_size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-H" || meta.long_name == "--si") {
      output.human = false;
      output.si = true;
      output.block_size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-P" || meta.long_name == "--portability") {
      output.portability = true;
      // [GNU] -P uses 1024-blocks with the "Capacity" header; with
      // POSIXLY_CORRECT it becomes "512-blocks" (no B suffix, unlike the
      // non-portable "512B-blocks").
      output.block_size = 1024;
      output.block_label = "1024-blocks";
      if (std::getenv("POSIXLY_CORRECT") != nullptr) {
        output.block_size = 512;
        output.block_label = "512-blocks";
      }
      output.block_size_explicit = true;
      output.display_suffix.clear();
      continue;
    }

    if (meta.short_name == "-B" || meta.long_name == "--block-size") {
      auto value = std::get_if<std::string>(&occurrence.value);
      // [GNU] The diagnostic names the option spelling the user typed
      // ("invalid --block-size argument 'x'" vs "invalid -B argument 'x'").
      std::string opt_name = "--block-size";
      for (std::string_view a : ctx.raw_args) {
        if (a == "--block-size" ||
            (a.size() > 12 && a.starts_with("--block-size="))) {
          opt_name = "--block-size";
        } else if (a.size() >= 2 && a[0] == '-' && a[1] != '-' &&
                   a.find('B') != std::string_view::npos) {
          opt_name = "-B";
        }
      }
      auto bad = [&]() {
        return std::unexpected(std::string("invalid ") + opt_name +
                               " argument '" +
                               (value ? *value : std::string()) + "'");
      };
      if (!value) {
        return bad();
      }
      if (*value == "human-readable") {
        output.human = true;
        output.si = false;
        output.block_size_explicit = true;
        output.display_suffix.clear();
        continue;
      }
      if (*value == "si") {
        output.human = false;
        output.si = true;
        output.block_size_explicit = true;
        output.display_suffix.clear();
        continue;
      }

      auto parsed = parse_block_size(*value, &output.display_suffix);
      if (!parsed) {
        return bad();
      }
      output.human = false;
      output.si = false;
      output.block_size = *parsed;
      output.block_label = block_label_for(*value);
      output.block_size_explicit = true;
    }
  }

  // [GNU] With no size option, df reads its block size from the first set of
  // DF_BLOCK_SIZE, BLOCK_SIZE, BLOCKSIZE (uutils #1173).  "human-readable"
  // and "si" select those modes; an unparseable or zero value is ignored.
  if (!output.block_size_explicit && !output.human && !output.si) {
    for (const char* name : {"DF_BLOCK_SIZE", "BLOCK_SIZE", "BLOCKSIZE"}) {
      const char* value = std::getenv(name);
      if (value == nullptr) continue;
      const std::string spec(value);
      if (spec == "human-readable") {
        output.human = true;
      } else if (spec == "si") {
        output.si = true;
      } else if (auto parsed = parse_block_size(spec)) {
        output.block_size = *parsed;
        output.block_label = block_label_for_size(*parsed);
      }
      break;  // the first set variable wins even when unparseable
    }
  }

  // [GNU] POSIXLY_CORRECT changes the default block size to 512B
  // ("512B-blocks") unless a size mode was requested explicitly.
  if (!output.block_size_explicit && !output.human && !output.si &&
      std::getenv("POSIXLY_CORRECT") != nullptr) {
    output.block_size = 512;
    output.block_label = "512B-blocks";
  }

  return output;
}

auto format_block_value(uint64_t value, const OutputConfig& output)
    -> std::string {
  if (output.human || output.si) {
    return format_size(value, output.si);
  }
  return std::to_string(ceil_div(value, output.block_size)) +
         output.display_suffix;
}

auto format_percent(uint64_t used, uint64_t available) -> std::string {
  uint64_t denominator = used + available;
  if (denominator == 0 || denominator < used) {
    return "-";
  }

  uint64_t percent = 0;
  if (used <= std::numeric_limits<uint64_t>::max() / 100) {
    uint64_t used_times_100 = used * 100;
    percent = used_times_100 / denominator +
              (used_times_100 % denominator != 0 ? 1 : 0);
  } else {
    double value =
        (static_cast<double>(used) * 100.0) / static_cast<double>(denominator);
    percent = static_cast<uint64_t>(value);
    if (static_cast<double>(percent) < value) {
      ++percent;
    }
  }

  return std::to_string(percent) + "%";
}

auto output_cell(OutputField field, const DiskInfo& info,
                 std::string_view requested_path, const OutputConfig& output)
    -> std::string {
  uint64_t used = info.total - info.total_free;
  switch (field) {
    case OutputField::Source:
      return info.filesystem;
    case OutputField::FsType:
      return info.type;
    case OutputField::Size:
      return format_block_value(info.total, output);
    case OutputField::Used:
      return format_block_value(used, output);
    case OutputField::Avail:
      return format_block_value(info.available, output);
    case OutputField::Pcent:
      return format_percent(used, info.available);
    case OutputField::ITotal:
    case OutputField::IUsed:
    case OutputField::IAvail:
    case OutputField::IPcent:
      // Windows volume APIs do not expose GNU-compatible inode accounting.
      return "-";
    case OutputField::Target:
      return info.mount_point;
    case OutputField::File:
      return std::string(requested_path);
  }
  return "";
}

auto total_output_cell(OutputField field, uint64_t total, uint64_t used,
                       uint64_t available, const OutputConfig& output)
    -> std::string {
  switch (field) {
    case OutputField::Source:
    case OutputField::File:
    case OutputField::Target:
      return "total";
    case OutputField::FsType:
    case OutputField::ITotal:
    case OutputField::IUsed:
    case OutputField::IAvail:
    case OutputField::IPcent:
      return "-";
    case OutputField::Size:
      return format_block_value(total, output);
    case OutputField::Used:
      return format_block_value(used, output);
    case OutputField::Avail:
      return format_block_value(available, output);
    case OutputField::Pcent:
      return format_percent(used, available);
  }
  return "";
}

auto print_custom_output_table(
    const std::vector<OutputColumn>& columns,
    const std::vector<std::vector<std::string>>& rows) -> void {
  std::vector<size_t> widths;
  widths.reserve(columns.size());
  for (const auto& column : columns) {
    widths.push_back(column.header.size());
  }

  for (const auto& row : rows) {
    for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
      widths[i] = std::max(widths[i], row[i].size());
    }
  }

  auto print_row = [&](const std::vector<std::string>& cells) {
    std::ostringstream line;
    for (size_t i = 0; i < cells.size(); ++i) {
      if (i > 0) line << ' ';
      if (columns[i].align_right) {
        line << std::right << std::setw(static_cast<int>(widths[i]))
             << cells[i];
      } else {
        line << std::left << std::setw(static_cast<int>(widths[i])) << cells[i];
      }
    }
    safePrintLn(line.str());
  };

  std::vector<std::string> headers;
  headers.reserve(columns.size());
  for (const auto& column : columns) {
    headers.push_back(column.header);
  }
  print_row(headers);
  for (const auto& row : rows) {
    print_row(row);
  }
}

auto print_total_row(uint64_t total, uint64_t used, uint64_t available,
                     const OutputConfig& output, bool print_type, bool inodes)
    -> void {
  safePrint("total");
  if (print_type) {
    safePrint(" -");
  }

  if (inodes) {
    safePrint("            -          -          -     -");
  } else {
    print_size_columns(total, used, available, output);

    safePrint(" ");
    safePrint(format_percent(used, available));
  }

  safePrintLn("  total");
}

/**
 * @brief Print disk usage information
 * @param ctx Command context
 * @return Result with success status
 */
auto print_disk_usage(const CommandContext<DF_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  // Use SmallVector for file paths (max 32 drives) - all stack-allocated
  SmallVector<std::string, 32> paths{};
  const bool auto_enumerated_drives = ctx.positionals.empty();

  if (auto_enumerated_drives) {
    DWORD logical_drives = GetLogicalDrives();
    if (logical_drives == 0) {
      return std::unexpected("cannot enumerate logical drives");
    }
    for (unsigned int index = 0; index < 26; ++index) {
      if ((logical_drives & (1u << index)) == 0) continue;
      std::string root;
      root.push_back(static_cast<char>('A' + index));
      root += ":\\";
      if (GetDriveTypeW(utf8_to_wstring(root).c_str()) != DRIVE_NO_ROOT_DIR) {
        paths.push_back(std::move(root));
      }
    }
    if (paths.empty()) {
      return std::unexpected("no accessible logical drives");
    }
  } else {
    for (const auto& arg : ctx.positionals) {
      std::string file_arg(arg);
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            paths.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      paths.push_back(file_arg);
    }
  }

  auto output = configure_output(ctx);
  if (!output) {
    return std::unexpected(output.error());
  }

  bool print_type =
      ctx.get<bool>("--print-type", false) || ctx.get<bool>("-T", false);
  bool inodes = ctx.get<bool>("--inodes", false) || ctx.get<bool>("-i", false);
  bool total = ctx.get<bool>("--total", false);
  bool custom_output = ctx.has("--output");
  bool all_fs = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  bool local_only =
      (ctx.get<bool>("--local", false) || ctx.get<bool>("-l", false)) &&
      !all_fs;
  std::string include_type = ctx.get<std::string>("--type", "");
  if (include_type.empty()) include_type = ctx.get<std::string>("-t", "");
  if (include_type.empty()) include_type = ctx.get<std::string>("-F", "");
  std::string exclude_type = ctx.get<std::string>("--exclude-type", "");
  if (exclude_type.empty()) exclude_type = ctx.get<std::string>("-x", "");

  if (custom_output) {
    if (inodes) {
      return std::unexpected("options -i and --output are mutually exclusive");
    }
    if (print_type) {
      return std::unexpected("options -T and --output are mutually exclusive");
    }
    if (output->portability) {
      return std::unexpected("options -P and --output are mutually exclusive");
    }
    if (!output->block_size_explicit && !output->human && !output->si) {
      output->block_size = 1024;
      output->block_label = "1K-blocks";
    }
  }

  std::vector<OutputColumn> custom_columns;
  if (custom_output) {
    auto columns = parse_output_columns(ctx, *output);
    if (!columns) {
      return std::unexpected(columns.error());
    }
    custom_columns = std::move(*columns);
  }

  bool all_ok = true;
  bool header_printed = false;
  uint64_t total_size = 0;
  uint64_t total_used = 0;
  uint64_t total_available = 0;
  size_t printed_rows = 0;

  // [GNU] df pads every column to the widest cell, so rows must be
  // buffered and rendered after all widths are known (df.c
  // header_mode/print_disk_info_full).
  struct PendingRow {
    std::string filesystem;
    std::string type;
    std::string total_cell;
    std::string used_cell;
    std::string avail_cell;
    std::string use_cell;
    std::string mount;
  };
  auto render_size_cell = [&](uint64_t value) -> std::string {
    if (output->human || output->si) {
      return format_size(value, output->si);
    }
    return std::to_string(
               static_cast<uintmax_t>(ceil_div(value, output->block_size))) +
           output->display_suffix;
  };
  auto render_size_header = [&]() -> std::string {
    if (output->human || output->si) return "Size";
    return output->block_label;
  };
  auto render_used_header = [&]() -> std::string {
    if (output->human || output->si) return "Used";
    return "Used";
  };
  auto render_avail_header = [&]() -> std::string {
    if (output->human || output->si) return "Avail";
    return "Available";
  };
  auto render_use_header = [&]() -> std::string {
    // [GNU] POSIX -P mode uses the historical "Capacity" header.
    return output->portability ? "Capacity" : "Use%";
  };
  std::vector<PendingRow> pending_rows;
  std::vector<std::vector<std::string>> custom_rows;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];

    // [GNU] df stats each command-line operand: a missing path reports
    // "df: <path>: No such file or directory" and a trailing-slash operand
    // on a non-directory reports "df: <path>: Not a directory"; the run
    // fails (exit 1) but valid operands are still listed (uutils #12777).
    if (!auto_enumerated_drives) {
      std::wstring wpath = utf8_to_wstring(path);
      std::wstring lookup = wpath;
      bool needs_directory = false;
      while (lookup.size() > 1 &&
             (lookup.back() == L'/' || lookup.back() == L'\\')) {
        lookup.pop_back();
        needs_directory = true;
      }
      std::error_code ec;
      const auto status =
          std::filesystem::status(std::filesystem::path(lookup), ec);
      if (ec || !std::filesystem::exists(status)) {
        safeErrorPrint("df: ");
        safeErrorPrint(path);
        safeErrorPrint(": No such file or directory\n");
        all_ok = false;
        continue;
      }
      if (needs_directory && !std::filesystem::is_directory(status)) {
        safeErrorPrint("df: ");
        safeErrorPrint(path);
        safeErrorPrint(": Not a directory\n");
        all_ok = false;
        continue;
      }
    }

    auto disk_info = get_disk_info(path);

    if (!disk_info) {
      if (!auto_enumerated_drives) {
        safeErrorPrint("df: cannot access '");
        safeErrorPrint(path);
        safeErrorPrint("': No such file or directory\n");
        all_ok = false;
      }
      continue;
    }

    const auto& info = *disk_info;
    uint64_t used = info.total - info.total_free;

    // Filter by type
    if (!include_type.empty()) {
      std::string lower_type = info.type;
      std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(),
                     ::tolower);
      std::string lower_include = include_type;
      std::transform(lower_include.begin(), lower_include.end(),
                     lower_include.begin(), ::tolower);
      if (lower_type != lower_include) continue;
    }

    if (!exclude_type.empty()) {
      std::string lower_type = info.type;
      std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(),
                     ::tolower);
      std::string lower_exclude = exclude_type;
      std::transform(lower_exclude.begin(), lower_exclude.end(),
                     lower_exclude.begin(), ::tolower);
      if (lower_type == lower_exclude) continue;
    }

    // Filter out network drives if --local
    if (local_only) {
      UINT drive_type =
          GetDriveTypeW(utf8_to_wstring(info.mount_point).c_str());
      if (drive_type == DRIVE_REMOTE) continue;
    }

    if (custom_output) {
      std::vector<std::string> row;
      row.reserve(custom_columns.size());
      for (const auto& column : custom_columns) {
        row.push_back(output_cell(column.field, info, path, *output));
      }
      custom_rows.push_back(std::move(row));
      header_printed = true;
    } else {
      header_printed = true;
      PendingRow row;
      row.filesystem = info.filesystem;
      row.type = info.type;
      row.mount = info.mount_point;
      if (inodes) {
        // [PLATFORM-OK] Windows volumes expose no inode counts; GNU on
        // Windows-adjacent filesystems also prints '-' placeholders.
        row.total_cell = "-";
        row.used_cell = "-";
        row.avail_cell = "-";
        row.use_cell = "-";
      } else {
        row.total_cell = render_size_cell(info.total);
        row.used_cell = render_size_cell(used);
        row.avail_cell = render_size_cell(info.available);
        row.use_cell = format_percent(used, info.available);
      }
      pending_rows.push_back(std::move(row));
    }
    total_size += info.total;
    total_used += used;
    total_available += info.available;
    ++printed_rows;
  }

  if (custom_output) {
    if (total && !custom_rows.empty()) {
      std::vector<std::string> row;
      row.reserve(custom_columns.size());
      for (const auto& column : custom_columns) {
        row.push_back(total_output_cell(column.field, total_size, total_used,
                                        total_available, *output));
      }
      custom_rows.push_back(std::move(row));
    }
    print_custom_output_table(custom_columns, custom_rows);
    return all_ok;
  }

  // [GNU] the header prints even when no filesystem matched the -t/-x
  // filters (df exits 0 with a bare header row).
  if (!pending_rows.empty() || all_ok) {
    // [GNU] Column width = max(header width, widest cell); numeric
    // columns right-align, filesystem/type/mount left-align, columns
    // separated by one space.
    auto width_of = [](const std::string& text) -> size_t {
      return utf8_to_wstring(text).size();
    };
    std::string fs_header = "Filesystem";
    std::string type_header = "Type";
    std::string size_header = inodes ? "Inodes" : render_size_header();
    std::string used_header = inodes ? "IUsed" : render_used_header();
    std::string avail_header = inodes ? "IFree" : render_avail_header();
    std::string use_header = inodes ? "IUse%" : render_use_header();
    if (inodes) {
      size_header = "Inodes";
      used_header = "IUsed";
      avail_header = "IFree";
      use_header = "IUse%";
    }
    size_t fs_w = width_of(fs_header);
    size_t type_w = width_of(type_header);
    size_t size_w = width_of(size_header);
    size_t used_w = width_of(used_header);
    size_t avail_w = width_of(avail_header);
    size_t use_w = width_of(use_header);
    for (const auto& row : pending_rows) {
      fs_w = std::max(fs_w, width_of(row.filesystem));
      type_w = std::max(type_w, width_of(row.type));
      size_w = std::max(size_w, width_of(row.total_cell));
      used_w = std::max(used_w, width_of(row.used_cell));
      avail_w = std::max(avail_w, width_of(row.avail_cell));
      use_w = std::max(use_w, width_of(row.use_cell));
    }
    PendingRow total_row;
    if (total && printed_rows > 0) {
      total_row.filesystem = "total";
      total_row.type = "-";
      if (inodes) {
        total_row.total_cell = "-";
        total_row.used_cell = "-";
        total_row.avail_cell = "-";
        total_row.use_cell = "-";
      } else {
        total_row.total_cell = render_size_cell(total_size);
        total_row.used_cell = render_size_cell(total_used);
        total_row.avail_cell = render_size_cell(total_available);
        total_row.use_cell = format_percent(total_used, total_available);
      }
      fs_w = std::max(fs_w, width_of(total_row.filesystem));
      size_w = std::max(size_w, width_of(total_row.total_cell));
      used_w = std::max(used_w, width_of(total_row.used_cell));
      avail_w = std::max(avail_w, width_of(total_row.avail_cell));
      use_w = std::max(use_w, width_of(total_row.use_cell));
    }

    auto print_left = [&](const std::string& cell, size_t width) {
      safePrint(cell);
      safePrint(std::string(width - width_of(cell), ' '));
    };
    auto print_right = [&](const std::string& cell, size_t width) {
      safePrint(std::string(width - width_of(cell), ' '));
      safePrint(cell);
    };

    auto print_table_row = [&](const PendingRow& row) {
      print_left(row.filesystem, fs_w);
      safePrint(" ");
      if (print_type) {
        print_left(row.type, type_w);
        safePrint(" ");
      }
      print_right(row.total_cell, size_w);
      safePrint(" ");
      print_right(row.used_cell, used_w);
      safePrint(" ");
      print_right(row.avail_cell, avail_w);
      safePrint(" ");
      print_right(row.use_cell, use_w);
      if (!row.mount.empty()) {
        safePrint(" ");
        safePrintLn(row.mount);
      } else {
        safePrintLn("");
      }
    };

    PendingRow header;
    header.filesystem = fs_header;
    header.type = type_header;
    header.total_cell = size_header;
    header.used_cell = used_header;
    header.avail_cell = avail_header;
    header.use_cell = use_header;
    header.mount = "Mounted on";
    print_table_row(header);
    for (const auto& row : pending_rows) {
      print_table_row(row);
    }
    if (total && printed_rows > 0) {
      print_table_row(total_row);
    }
  }

  return all_ok;
}

}  // namespace df_pipeline

REGISTER_COMMAND(
    df,
    /* name */
    "df",

    /* synopsis */
    "report file system disk space usage",

    /* description */
    "The df command displays the amount of available disk space on file\n"
    "systems of which the invoking user has adequate read access.\n\n"
    "On Windows, it reports information about volumes that contain the\n"
    "specified paths, including total size, used space, and available space.",

    /* examples */
    "  df\n"
    "  df -h C:\\Users\n"
    "  df -k\n"
    "  df -T\n"
    "  df -t NTFS\n"
    "  df -x tmpfs\n"
    "  df -l",

    /* see_also */
    "du(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    DF_OPTIONS) {
  using namespace df_pipeline;

  // [DIFFERS] GNU: -v is "(ignored)"; WinuxCmd accepts it for compatibility
  (void)ctx.get<bool>("-v", false);

  // [GNU] --sync/--no-sync: sync control
  // On Windows, the OS maintains filesystem cache consistency.
  // These flags are accepted for compatibility but have no effect.

  auto result = print_disk_usage(ctx);
  if (!result) {
    cp::report_error(result, L"df");
    return 1;
  }

  return *result ? 0 : 1;
}
