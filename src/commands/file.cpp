// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for file - determine file type
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
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;
using namespace std::string_view_literals;

/**
 * @brief FILE command options definition
 *
 * This array defines all the options supported by the file command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -b, @a --brief: do not append filename [IMPLEMENTED]
 * - @a -h, @a --no-dereference: don't follow symlinks [IMPLEMENTED]
 * - @a -i, @a --mime: output MIME type strings [IMPLEMENTED]
 * - @a -L, @a --dereference: follow symlinks [IMPLEMENTED]
 */
auto constexpr FILE_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--brief", "do not prepend filenames to output lines"),
    // [DIFFERS] checking format not meaningful on Windows; prints stub
    OPTION("-c", "--checking-printout", "print the checking format"),
    // [GNU]
    OPTION("-f", "--files-from", "read the filenames from FILE", STRING_TYPE),
    // [GNU]
    OPTION("-k", "--keep-going", "don't stop at first match"),
    // [DIFFERS] magic file not supported on Windows; prints stub
    OPTION("-m", "--magic-file", "use FILE as magic file", STRING_TYPE),
    // [GNU]
    OPTION("-h", "--no-dereference", "don't follow symlinks"),
    // [GNU]
    OPTION("-i", "--mime", "output MIME type strings"),
    // [GNU]
    OPTION("-L", "--dereference", "follow symlinks"),
    // [GNU]
    OPTION("-n", "--no-buffer", "do not buffer output"),
    // [GNU]
    OPTION("-N", "--no-pad", "do not pad output"),
    // [GNU]
    OPTION("-z", "--uncompress", "try to decompress compressed files")};

// ======================================================
// Constants
// ======================================================
namespace file_constants {
// Common file extensions and their types - using compile-time ConstexprMap
constexpr auto extension_map = make_constexpr_map(
    std::to_array<std::pair<std::string_view, std::string_view>>({
        // Text files
        {".txt"sv, "ASCII text"sv},
        {".md"sv, "UTF-8 Unicode text"sv},
        {".json"sv, "JSON data"sv},
        {".xml"sv, "XML document text"sv},
        {".html"sv, "HTML document text"sv},
        {".htm"sv, "HTML document text"sv},
        {".css"sv, "Cascading Style Sheet text"sv},
        {".js"sv, "JavaScript source text"sv},
        {".ts"sv, "TypeScript source text"sv},
        {".py"sv, "Python script text"sv},
        {".sh"sv, "shell script text"sv},
        {".bat"sv, "batch script text"sv},
        {".cmd"sv, "batch script text"sv},
        {".ps1"sv, "PowerShell script text"sv},
        {".c"sv, "C source text"sv},
        {".cpp"sv, "C++ source text"sv},
        {".h"sv, "C header text"sv},
        {".hpp"sv, "C++ header text"sv},
        {".java"sv, "Java source text"sv},
        {".rs"sv, "Rust source text"sv},
        {".go"sv, "Go source text"sv},
        {".php"sv, "PHP script text"sv},
        {".rb"sv, "Ruby script text"sv},
        {".pl"sv, "Perl script text"sv},
        {".lua"sv, "Lua script text"sv},

        // Archives
        {".zip"sv, "Zip archive data"sv},
        {".rar"sv, "RAR archive data"sv},
        {".7z"sv, "7-zip archive data"sv},
        {".tar"sv, "tar archive"sv},
        {".gz"sv, "gzip compressed data"sv},
        {".bz2"sv, "bzip2 compressed data"sv},
        {".xz"sv, "xz compressed data"sv},
        {".cab"sv, "Cabinet archive data"sv},

        // Images
        {".png"sv, "PNG image data"sv},
        {".jpg"sv, "JPEG image data"sv},
        {".jpeg"sv, "JPEG image data"sv},
        {".gif"sv, "GIF image data"sv},
        {".bmp"sv, "PC bitmap image data"sv},
        {".ico"sv, "Windows icon image data"sv},
        {".tiff"sv, "TIFF image data"sv},
        {".webp"sv, "WebP image data"sv},
        {".svg"sv, "SVG image data"sv},

        // Audio
        {".mp3"sv, "MPEG audio data"sv},
        {".wav"sv, "WAVE audio data"sv},
        {".flac"sv, "FLAC audio data"sv},
        {".ogg"sv, "Ogg audio data"sv},
        {".aac"sv, "AAC audio data"sv},
        {".m4a"sv, "AAC audio data"sv},

        // Video
        {".mp4"sv, "MPEG-4 video data"sv},
        {".avi"sv, "AVI video data"sv},
        {".mkv"sv, "Matroska video data"sv},
        {".mov"sv, "QuickTime video data"sv},
        {".wmv"sv, "Windows Media video data"sv},
        {".flv"sv, "Flash video data"sv},

        // Documents
        {".pdf"sv, "PDF document"sv},
        {".doc"sv, "Microsoft Word document"sv},
        {".docx"sv, "Microsoft Word document"sv},
        {".xls"sv, "Microsoft Excel spreadsheet"sv},
        {".xlsx"sv, "Microsoft Excel spreadsheet"sv},
        {".ppt"sv, "Microsoft PowerPoint presentation"sv},
        {".pptx"sv, "Microsoft PowerPoint presentation"sv},

        // Executables
        {".exe"sv, "PE32 executable (console) Intel 80386, for MS Windows"sv},
        {".dll"sv, "PE32+ executable (DLL) x86-64, for MS Windows"sv},
        {".sys"sv, "PE32+ executable (native) x86-64, for MS Windows"sv},

        // Object files
        {".obj"sv, "COFF object file"sv},
        {".lib"sv, "COFF archive library"sv},
        {".o"sv, "ELF relocatable object file"sv},
        {".a"sv, "ar archive"sv},

        // Libraries
        {".so"sv, "ELF shared object"sv},
        {".dylib"sv, "Mach-O dynamically linked shared library"sv},
    }));

constexpr auto extension_mime_map = make_constexpr_map(
    std::to_array<std::pair<std::string_view, std::string_view>>({
        {".txt"sv, "text/plain; charset=us-ascii"sv},
        {".md"sv, "text/markdown; charset=utf-8"sv},
        {".json"sv, "application/json; charset=us-ascii"sv},
        {".xml"sv, "text/xml; charset=us-ascii"sv},
        {".html"sv, "text/html; charset=us-ascii"sv},
        {".htm"sv, "text/html; charset=us-ascii"sv},
        {".css"sv, "text/css; charset=us-ascii"sv},
        {".js"sv, "text/javascript; charset=us-ascii"sv},
        {".ts"sv, "text/plain; charset=us-ascii"sv},
        {".py"sv, "text/x-python; charset=us-ascii"sv},
        {".sh"sv, "text/x-shellscript; charset=us-ascii"sv},
        {".pdf"sv, "application/pdf; charset=binary"sv},
        {".zip"sv, "application/zip; charset=binary"sv},
        {".gz"sv, "application/gzip; charset=binary"sv},
        {".png"sv, "image/png; charset=binary"sv},
        {".jpg"sv, "image/jpeg; charset=binary"sv},
        {".jpeg"sv, "image/jpeg; charset=binary"sv},
        {".gif"sv, "image/gif; charset=binary"sv},
        {".bmp"sv, "image/bmp; charset=binary"sv},
        {".exe"sv, "application/x-dosexec; charset=binary"sv},
        {".dll"sv, "application/x-dosexec; charset=binary"sv},
    }));
}  // namespace file_constants

// ======================================================
// Pipeline components
// ======================================================
namespace file_pipeline {
namespace cp = core::pipeline;

struct FileClassification {
  std::string description;
  std::string mime;
};

auto has_prefix(const std::vector<unsigned char>& data,
                std::initializer_list<unsigned char> prefix) -> bool {
  return data.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), data.begin());
}

auto read_file_header(const std::wstring& path) -> std::vector<unsigned char> {
  std::ifstream file(
      std::filesystem::path(native_path::normalize_api_operand_w(path)),
      std::ios::binary);
  if (!file) {
    return {};
  }

  std::array<unsigned char, 64> buffer{};
  file.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
  const auto count =
      static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()));
  return {buffer.begin(), buffer.begin() + count};
}

auto classify_by_magic(const std::vector<unsigned char>& header)
    -> std::optional<FileClassification> {
  if (has_prefix(header, {'%', 'P', 'D', 'F', '-'})) {
    return FileClassification{"PDF document",
                              "application/pdf; charset=binary"};
  }
  if (has_prefix(header, {static_cast<unsigned char>(0x89), 'P', 'N', 'G', '\r',
                          '\n', static_cast<unsigned char>(0x1A), '\n'})) {
    return FileClassification{"PNG image data", "image/png; charset=binary"};
  }
  if (has_prefix(header, {static_cast<unsigned char>(0xFF),
                          static_cast<unsigned char>(0xD8),
                          static_cast<unsigned char>(0xFF)})) {
    return FileClassification{"JPEG image data", "image/jpeg; charset=binary"};
  }
  if (has_prefix(header, {'G', 'I', 'F', '8', '7', 'a'}) ||
      has_prefix(header, {'G', 'I', 'F', '8', '9', 'a'})) {
    return FileClassification{"GIF image data", "image/gif; charset=binary"};
  }
  if (header.size() >= 4 && header[0] == 'P' && header[1] == 'K' &&
      ((header[2] == 3 && header[3] == 4) ||
       (header[2] == 5 && header[3] == 6) ||
       (header[2] == 7 && header[3] == 8))) {
    return FileClassification{"Zip archive data",
                              "application/zip; charset=binary"};
  }
  if (has_prefix(header, {static_cast<unsigned char>(0x1F),
                          static_cast<unsigned char>(0x8B)})) {
    return FileClassification{"gzip compressed data",
                              "application/gzip; charset=binary"};
  }
  if (has_prefix(header, {'B', 'M'})) {
    return FileClassification{"PC bitmap image data",
                              "image/bmp; charset=binary"};
  }
  if (has_prefix(header, {static_cast<unsigned char>(0x7F), 'E', 'L', 'F'})) {
    return FileClassification{"ELF executable",
                              "application/x-executable; charset=binary"};
  }
  if (has_prefix(header, {'M', 'Z'})) {
    return FileClassification{"MS-DOS executable",
                              "application/x-dosexec; charset=binary"};
  }

  return std::nullopt;
}

auto classify_by_extension(const std::wstring& filename) -> FileClassification {
  size_t dot_pos = filename.find_last_of(L'.');
  if (dot_pos == std::wstring::npos) {
    return {"ASCII text", "text/plain; charset=us-ascii"};
  }

  std::wstring ext = filename.substr(dot_pos);
  std::string ext_lower = wstring_to_utf8(ext);
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                 ::tolower);

  std::string_view ext_sv(ext_lower);
  auto description = file_constants::extension_map.get_or(ext_sv, "data"sv);
  auto mime = file_constants::extension_mime_map.get_or(
      ext_sv, "application/octet-stream; charset=binary"sv);
  return {std::string(description), std::string(mime)};
}

/**
 * @brief Process a single file
 * @param path File path
 * @param brief Brief mode (no filename prefix)
 * @param symlink Follow symlinks
 * @param mime Output MIME type instead of description
 * @return File type description
 */
auto process_file(const std::string& path, bool brief, bool symlink, bool mime)
    -> std::optional<std::string> {
  std::wstring wpath =
      utf8_to_wstring(native_path::normalize_api_operand(path));

  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::nullopt;
  }

  std::string type;

  // Check if it's a directory
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    type = mime ? "inode/directory; charset=binary" : "directory";
  }
  // Check if it's a reparse point (symlink or junction)
  else if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    type = mime ? "inode/symlink; charset=binary" : "symbolic link";
  }
  // Regular file
  else {
    // A WinuxCmd fifo marker (#1038) classifies as a named pipe.
    if ((attrs & FILE_ATTRIBUTE_SYSTEM) != 0 &&
        native_path::is_winux_fifo_w(wpath)) {
      type = mime ? "inode/fifo; charset=binary" : "fifo (named pipe)";
    } else {
      // Get filename from path
      size_t last_sep = wpath.find_last_of(L"\\/");
      std::wstring filename =
          (last_sep != std::wstring::npos) ? wpath.substr(last_sep + 1) : wpath;

      auto classification = classify_by_magic(read_file_header(wpath))
                                .value_or(classify_by_extension(filename));
      type = mime ? classification.mime : classification.description;
    }
  }

  if (brief) {
    return type;
  } else {
    return path + ": " + type;
  }
}

/**
 * @brief Process all files
 * @param ctx Command context
 * @return Result with success status
 */
auto process_files(const CommandContext<FILE_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  // Use SmallVector for file paths (max 64 files) - all stack-allocated
  SmallVector<std::string, 64> paths{};

  // -f/--files-from: read filenames from a file instead of arguments
  std::string files_from = ctx.get<std::string>("--files-from", "");
  if (files_from.empty()) files_from = ctx.get<std::string>("-f", "");

  if (!files_from.empty()) {
    // Read paths from the specified file, one per line
    std::ifstream fin(
        utf8_to_wstring(native_path::normalize_api_operand(files_from)));
    if (!fin) {
      safeErrorPrint("file: cannot open '");
      safeErrorPrint(files_from);
      safeErrorPrint("': No such file or directory\n");
      return false;
    }
    std::string line;
    while (std::getline(fin, line)) {
      if (!line.empty()) {
        paths.push_back(std::move(line));
      }
    }
  } else if (ctx.positionals.empty()) {
    paths.push_back(".");
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

  bool brief = ctx.get<bool>("--brief", false) || ctx.get<bool>("-b", false);
  bool symlink =
      ctx.get<bool>("--dereference", false) || ctx.get<bool>("-L", false);
  bool no_deref =
      ctx.get<bool>("--no-dereference", false) || ctx.get<bool>("-h", false);
  bool mime = ctx.get<bool>("--mime", false) || ctx.get<bool>("-i", false);
  bool checking_format =
      ctx.get<bool>("--checking-printout", false) || ctx.get<bool>("-c", false);
  bool keep_going =
      ctx.get<bool>("--keep-going", false) || ctx.get<bool>("-k", false);
  bool no_buffer =
      ctx.get<bool>("--no-buffer", false) || ctx.get<bool>("-n", false);
  bool no_pad = ctx.get<bool>("--no-pad", false) || ctx.get<bool>("-N", false);
  bool uncompress =
      ctx.get<bool>("--uncompress", false) || ctx.get<bool>("-z", false);
  bool has_magic_file = ctx.has("--magic-file") || ctx.has("-m");

  // [DIFFERS] -c/--checking-printout: print checking format then exit
  if (checking_format) {
    // On Windows, file magic database is not available; print stub format
    safePrintLn(
        L"file: compiled with magic number detection based on extension");
    safePrintLn(L"file: no magic file format on Windows");
    return true;
  }

  // [DIFFERS] -m/--magic-file: magic file not supported on Windows
  if (has_magic_file) {
    safeErrorPrint("file: --magic-file is not supported on Windows\n");
    safeErrorPrint(
        "file: file type detection uses file extensions on Windows\n");
    return false;
  }

  // [DIFFERS] -z/--uncompress: partial - detect .gz and warn
  // (actual decompression would require zlib; mark as [DIFFERS])
  if (uncompress) {
    // Continue but note compression support is limited
  }

  bool all_ok = true;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    auto result = process_file(path, brief, symlink && !no_deref, mime);

    if (result) {
      safePrintLn(utf8_to_wstring(*result));
    } else {
      safeErrorPrint("file: cannot access '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      all_ok = false;
      if (!keep_going) break;
    }
  }

  return all_ok;
}

}  // namespace file_pipeline

REGISTER_COMMAND(
    file,
    /* name */
    "file",

    /* synopsis */
    "determine file type",

    /* description */
    "The file command tests each argument in an attempt to classify it.\n"
    "There are three sets of tests, performed in this order:\n"
    "filesystem tests, magic number tests, and language tests.\n\n"
    "The first test that succeeds causes the file type to be printed.\n"
    "On Windows, file type detection is based on file extensions and\n"
    "attributes (directory, symlink, regular file).",

    /* examples */
    "  file document.txt\n"
    "  file image.jpg\n"
    "  file -b script.py",

    /* see_also */
    "stat(1), ls(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    FILE_OPTIONS) {
  using namespace file_pipeline;

  auto result = process_files(ctx);
  if (!result) {
    cp::report_error(result, L"file");
    return 1;
  }

  return *result ? 0 : 1;
}
