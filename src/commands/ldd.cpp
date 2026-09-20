// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Print PE import dependencies.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LDD_OPTIONS = std::array{
    // [GNU] -d, --data-relocs: process data relocations
    OPTION("-d", "--data-relocs", "process data relocations"),
    // [GNU] -r, --function-relocs: process data and function relocations
    OPTION("-r", "--function-relocs", "process data and function relocations"),
    // [GNU]
    OPTION("-n", "--name", "print only imported DLL names"),
    // [GNU]
    OPTION("-u", "--unused", "print unused direct dependencies"),
    // [GNU]
    OPTION("-v", "--verbose", "print all information")};

namespace ldd_pipeline {

struct Section {
  DWORD virtual_address = 0;
  DWORD virtual_size = 0;
  DWORD raw_offset = 0;
  DWORD raw_size = 0;
};

template <typename T>
auto read_struct(const std::vector<std::byte>& data, size_t offset)
    -> const T* {
  if (offset > data.size() || sizeof(T) > data.size() - offset) return nullptr;
  return reinterpret_cast<const T*>(data.data() + offset);
}

auto rva_to_offset(DWORD rva, std::span<const Section> sections)
    -> std::optional<size_t> {
  for (const auto& section : sections) {
    DWORD span = std::max(section.virtual_size, section.raw_size);
    if (rva >= section.virtual_address &&
        rva < section.virtual_address + span) {
      return static_cast<size_t>(section.raw_offset +
                                 (rva - section.virtual_address));
    }
  }
  return std::nullopt;
}

auto read_c_string(const std::vector<std::byte>& data, size_t offset)
    -> std::optional<std::string> {
  if (offset >= data.size()) return std::nullopt;
  std::string out;
  for (size_t i = offset; i < data.size(); ++i) {
    char ch = static_cast<char>(data[i]);
    if (ch == '\0') return out;
    out.push_back(ch);
    if (out.size() > MAX_PATH * 4) return std::nullopt;
  }
  return std::nullopt;
}

auto load_file(const std::string& path)
    -> std::expected<std::vector<std::byte>, std::string> {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::unexpected("cannot open '" + path + "'");
  file.seekg(0, std::ios::end);
  std::streamoff size = file.tellg();
  if (size < 0) return std::unexpected("cannot size '" + path + "'");
  file.seekg(0, std::ios::beg);

  std::vector<std::byte> data(static_cast<size_t>(size));
  if (!data.empty()) {
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) return std::unexpected("cannot read '" + path + "'");
  }
  return data;
}

auto imported_dlls(const std::vector<std::byte>& data)
    -> std::expected<std::vector<std::string>, std::string> {
  auto* dos = read_struct<IMAGE_DOS_HEADER>(data, 0);
  if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return std::unexpected("not a PE executable");
  }
  if (dos->e_lfanew < 0) return std::unexpected("invalid PE header");

  size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
  DWORD signature = 0;
  if (auto* sig = read_struct<DWORD>(data, nt_offset)) signature = *sig;
  if (signature != IMAGE_NT_SIGNATURE)
    return std::unexpected("invalid PE header");

  auto* file_header =
      read_struct<IMAGE_FILE_HEADER>(data, nt_offset + sizeof(DWORD));
  if (file_header == nullptr) return std::unexpected("truncated PE header");

  size_t optional_offset =
      nt_offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
  auto* magic = read_struct<WORD>(data, optional_offset);
  if (magic == nullptr) return std::unexpected("truncated optional header");

  IMAGE_DATA_DIRECTORY import_dir{};
  if (*magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    auto* nt = read_struct<IMAGE_NT_HEADERS64>(data, nt_offset);
    if (nt == nullptr) return std::unexpected("truncated PE32+ header");
    import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  } else if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    auto* nt = read_struct<IMAGE_NT_HEADERS32>(data, nt_offset);
    if (nt == nullptr) return std::unexpected("truncated PE32 header");
    import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  } else {
    return std::unexpected("unknown PE optional header");
  }

  std::vector<Section> sections;
  sections.reserve(file_header->NumberOfSections);
  size_t section_offset = optional_offset + file_header->SizeOfOptionalHeader;
  for (WORD i = 0; i < file_header->NumberOfSections; ++i) {
    auto* section = read_struct<IMAGE_SECTION_HEADER>(
        data, section_offset + i * sizeof(IMAGE_SECTION_HEADER));
    if (section == nullptr) return std::unexpected("truncated section table");
    sections.push_back(Section{.virtual_address = section->VirtualAddress,
                               .virtual_size = section->Misc.VirtualSize,
                               .raw_offset = section->PointerToRawData,
                               .raw_size = section->SizeOfRawData});
  }

  if (import_dir.VirtualAddress == 0) return std::vector<std::string>{};
  auto import_offset = rva_to_offset(import_dir.VirtualAddress, sections);
  if (!import_offset) return std::unexpected("invalid import table");

  std::vector<std::string> imports;
  for (size_t offset = *import_offset;;
       offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    auto* descriptor = read_struct<IMAGE_IMPORT_DESCRIPTOR>(data, offset);
    if (descriptor == nullptr) return std::unexpected("truncated import table");
    if (descriptor->OriginalFirstThunk == 0 && descriptor->FirstThunk == 0 &&
        descriptor->Name == 0) {
      break;
    }
    auto name_offset = rva_to_offset(descriptor->Name, sections);
    if (!name_offset) continue;
    auto name = read_c_string(data, *name_offset);
    if (name && !name->empty()) imports.push_back(*name);
  }

  std::ranges::sort(imports, [](std::string_view lhs, std::string_view rhs) {
    return ascii_lower_copy(lhs) < ascii_lower_copy(rhs);
  });
  imports.erase(
      std::ranges::unique(imports,
                          [](std::string_view lhs, std::string_view rhs) {
                            return ascii_iequals(lhs, rhs);
                          })
          .begin(),
      imports.end());
  return imports;
}

auto resolve_dll(const std::string& dll_name) -> std::string {
  std::wstring wide = utf8_to_wstring(dll_name);
  DWORD needed =
      SearchPathW(nullptr, wide.c_str(), nullptr, 0, nullptr, nullptr);
  if (needed == 0) return {};
  std::wstring buffer(needed, L'\0');
  DWORD written =
      SearchPathW(nullptr, wide.c_str(), nullptr,
                  static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
  if (written == 0) return {};
  buffer.resize(written);
  return wstring_to_utf8(buffer);
}

auto print_imports(const std::string& path, bool names_only) -> int {
  auto data = load_file(path);
  if (!data) {
    safeErrorPrintLn("ldd: " + data.error());
    return 1;
  }
  auto imports = imported_dlls(*data);
  if (!imports) {
    safeErrorPrintLn("ldd: " + path + ": " + imports.error());
    return 1;
  }
  for (const auto& dll : *imports) {
    if (names_only) {
      safePrintLn(dll);
      continue;
    }
    std::string resolved = resolve_dll(dll);
    safePrintLn(dll + " => " + (resolved.empty() ? "not found" : resolved));
  }
  return 0;
}

auto run(const CommandContext<LDD_OPTIONS.size()>& ctx) -> int {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("ldd: missing file operand");
    return 1;
  }
  bool names_only =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--name", false);
  bool unused_only =
      ctx.get<bool>("-u", false) || ctx.get<bool>("--unused", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

  if (verbose) {
    safePrintLn("  Version information:");
  }

  int status = 0;
  for (auto file : ctx.positionals) {
    if (unused_only) {
      // -u/--unused: print direct imports but mark all as "unused"
      // since on Windows we cannot determine actual usage
      auto data = load_file(std::string(file));
      if (!data) {
        safeErrorPrintLn("ldd: " + data.error());
        status = 1;
        continue;
      }
      auto imports = imported_dlls(*data);
      if (!imports) {
        safeErrorPrintLn("ldd: " + std::string(file) + ": " + imports.error());
        status = 1;
        continue;
      }
      for (const auto& dll : *imports) {
        safePrintLn("	" + dll);
      }
    } else if (print_imports(std::string(file), names_only) != 0) {
      status = 1;
    }
  }
  return status;
}

}  // namespace ldd_pipeline

REGISTER_COMMAND(ldd, "ldd", "ldd [OPTION]... FILE...",
                 "Print PE import dependencies for Windows executables.",
                 "  ldd winuxcmd.exe\n"
                 "  ldd --name winuxcmd.exe",
                 "pldd(1), file(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 LDD_OPTIONS) {
  return ldd_pipeline::run(ctx);
}
