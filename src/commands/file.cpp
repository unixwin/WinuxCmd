/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: file.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
 * - @a -i, @a --mime: output MIME type strings [TODO]
 * - @a -L, @a --dereference: follow symlinks [IMPLEMENTED]
 */
auto constexpr FILE_OPTIONS = std::array{
    OPTION("-b", "--brief", "do not prepend filenames to output lines"),
    OPTION("-h", "--no-dereference", "don't follow symlinks"),
    OPTION("-i", "--mime", "output MIME type strings [TODO]"),
    OPTION("-L", "--dereference", "follow symlinks")};

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
}  // namespace file_constants

// ======================================================
// Pipeline components
// ======================================================
namespace file_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Detect file type from extension
 * @param filename Filename to check
 * @return Detected file type
 */
auto detect_file_type(const std::wstring& filename) -> std::string {
  // Get extension
  size_t dot_pos = filename.find_last_of(L'.');
  if (dot_pos == std::wstring::npos) {
    // No extension, assume text file
    return "ASCII text";
  }

  std::wstring ext = filename.substr(dot_pos);
  std::string ext_lower = wstring_to_utf8(ext);

  // Convert to lowercase
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                 ::tolower);

  // Lookup in extension map using ConstexprMap
  std::string_view ext_sv(ext_lower);
  auto result = file_constants::extension_map.get_or(ext_sv, "data"sv);

  return std::string(result);
}

/**
 * @brief Process a single file
 * @param path File path
 * @param brief Brief mode (no filename prefix)
 * @param symlink Follow symlinks
 * @return File type description
 */
auto process_file(const std::string& path, bool brief, bool symlink)
    -> std::optional<std::string> {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::nullopt;
  }

  std::string type;

  // Check if it's a directory
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    type = "directory";
  }
  // Check if it's a reparse point (symlink or junction)
  else if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    type = "symbolic link";
  }
  // Regular file
  else {
    // Get filename from path
    size_t last_sep = wpath.find_last_of(L"\\/");
    std::wstring filename =
        (last_sep != std::wstring::npos) ? wpath.substr(last_sep + 1) : wpath;

    type = detect_file_type(filename);
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

  if (ctx.positionals.empty()) {
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

  bool all_ok = true;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    auto result = process_file(path, brief, symlink && !no_deref);

    if (result) {
      safePrintLn(utf8_to_wstring(*result));
    } else {
      safeErrorPrint("file: cannot access '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      all_ok = false;
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
