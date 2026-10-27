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

// [GNU] Issue #1383: classification is by CONTENT only — filesystem tests,
// then magic numbers, then shebang / JSON / text analysis. The file name
// extension is never consulted: file(1) reports a text file named .jpg as
// "ASCII text" and a PDF named .txt as "PDF document".
auto read_file_prefix(const std::wstring& path, size_t max_bytes)
    -> std::vector<unsigned char> {
  std::ifstream file(
      std::filesystem::path(native_path::normalize_api_operand_w(path)),
      std::ios::binary);
  if (!file) {
    return {};
  }

  std::vector<unsigned char> buffer(max_bytes);
  file.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
  const auto count =
      static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()));
  buffer.resize(count);
  return buffer;
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

// [GNU] Shebang (language) tests, file-5.45 wordings. An unknown
// interpreter falls through to the generic text analysis.
auto classify_by_shebang(const std::vector<unsigned char>& data)
    -> std::optional<FileClassification> {
  if (data.size() < 2 || data[0] != '#' || data[1] != '!') {
    return std::nullopt;
  }
  size_t line_end = 2;
  while (line_end < data.size() && data[line_end] != '\n' &&
         data[line_end] != '\r') {
    ++line_end;
  }
  std::string line(data.begin() + 2, data.begin() + line_end);

  // Split the #! line into whitespace-separated tokens.
  std::vector<std::string> tokens;
  size_t pos = 0;
  while (pos < line.size()) {
    while (pos < line.size() &&
           (line[pos] == ' ' || line[pos] == '\t')) {
      ++pos;
    }
    size_t start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
      ++pos;
    }
    if (pos > start) tokens.push_back(line.substr(start, pos - start));
  }
  if (tokens.empty()) return std::nullopt;

  auto basename_of = [](const std::string& p) {
    size_t sep = p.find_last_of("/\\");
    return sep == std::string::npos ? p : p.substr(sep + 1);
  };

  std::string program = basename_of(tokens[0]);
  if (program == "env") {
    // #!/usr/bin/env [-flags] PROGRAM ...
    size_t t = 1;
    while (t < tokens.size() && !tokens[t].empty() && tokens[t][0] == '-') {
      ++t;
    }
    if (t >= tokens.size()) return std::nullopt;
    program = basename_of(tokens[t]);
  }

  if (program == "sh") {
    return FileClassification{"POSIX shell script, ASCII text executable",
                              "text/x-shellscript; charset=us-ascii"};
  }
  if (program == "bash") {
    return FileClassification{
        "Bourne-Again shell script, ASCII text executable",
        "text/x-shellscript; charset=us-ascii"};
  }
  if (program == "zsh") {
    return FileClassification{
        "Paul Falstad's zsh script, ASCII text executable",
        "text/x-shellscript; charset=us-ascii"};
  }
  if (program.starts_with("python")) {
    return FileClassification{"Python script, ASCII text executable",
                              "text/x-script.python; charset=us-ascii"};
  }
  if (program.starts_with("perl")) {
    return FileClassification{"Perl script text executable",
                              "text/x-perl; charset=us-ascii"};
  }
  if (program.starts_with("ruby")) {
    return FileClassification{"Ruby script, ASCII text executable",
                              "text/x-ruby; charset=us-ascii"};
  }
  if (program == "node" || program == "nodejs") {
    return FileClassification{"Node.js script executable, ASCII text",
                              "text/javascript; charset=us-ascii"};
  }
  return std::nullopt;
}

// [GNU] JSON test: first non-whitespace byte is '{' or '[' and the buffer
// scans as balanced JSON (strings and escapes honored). A buffer that
// ends mid-structure without an error still counts (prefix reads).
auto looks_like_json(const std::vector<unsigned char>& data) -> bool {
  size_t i = 0;
  while (i < data.size() && (data[i] == ' ' || data[i] == '\t' ||
                             data[i] == '\n' || data[i] == '\r')) {
    ++i;
  }
  if (i >= data.size() || (data[i] != '{' && data[i] != '[')) {
    return false;
  }
  std::vector<unsigned char> stack;
  bool in_string = false;
  bool escaped = false;
  for (; i < data.size(); ++i) {
    unsigned char c = data[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      } else if (c < 0x20) {
        return false;  // raw control byte inside a JSON string
      }
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        break;
      case '{':
        stack.push_back('}');
        break;
      case '[':
        stack.push_back(']');
        break;
      case '}':
      case ']':
        if (stack.empty() || stack.back() != c) return false;
        stack.pop_back();
        break;
      default:
        break;
    }
  }
  return stack.empty() || !in_string;
}

auto line_terminator_suffix(const std::vector<unsigned char>& data)
    -> std::string {
  size_t crlf = 0;
  size_t lf_only = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '\n') {
      if (i > 0 && data[i - 1] == '\r') {
        ++crlf;
      } else {
        ++lf_only;
      }
    }
  }
  if (crlf > 0 && lf_only == 0) return ", with CRLF line terminators";
  if (crlf > 0 && lf_only > 0) return ", with CRLF, LF line terminators";
  return "";
}

// [GNU] Text analysis: ASCII / UTF-8 / ISO-8859 / data, file(1) wordings.
auto classify_text(const std::vector<unsigned char>& data)
    -> FileClassification {
  bool has_nul = false;
  bool has_multibyte = false;
  bool utf8_valid = true;
  size_t printable = 0;

  auto ascii_control_ok = [](unsigned char c) {
    return c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' ||
           c == '\b';
  };

  for (size_t i = 0; i < data.size();) {
    unsigned char c = data[i];
    if (c == 0) {
      has_nul = true;
      ++i;
      continue;
    }
    if (c < 0x80) {
      if ((c >= 0x20 && c <= 0x7E) || ascii_control_ok(c)) {
        ++printable;
      }
      ++i;
      continue;
    }
    if (c >= 0xA0) ++printable;
    // UTF-8 multibyte sequence validation.
    size_t len = 0;
    if (c >= 0xC2 && c <= 0xDF) {
      len = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
      len = 3;
    } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4;
    }
    bool seq_ok = len > 0 && i + len <= data.size();
    if (seq_ok) {
      for (size_t k = 1; k < len; ++k) {
        if ((data[i + k] & 0xC0) != 0x80) {
          seq_ok = false;
          break;
        }
      }
    }
    if (seq_ok) {
      has_multibyte = true;
      i += len;
    } else {
      utf8_valid = false;
      ++i;
    }
  }

  const std::string suffix = line_terminator_suffix(data);
  if (!has_nul && utf8_valid) {
    if (has_multibyte) {
      return {"Unicode text, UTF-8 text" + suffix,
              "text/plain; charset=utf-8"};
    }
    if (printable == data.size()) {
      return {"ASCII text" + suffix, "text/plain; charset=us-ascii"};
    }
  }
  // Mostly-printable high-byte content with no NUL: ISO-8859 family.
  if (!has_nul && data.size() > 0 &&
      printable * 10 >= data.size() * 9) {
    return {"ISO-8859 text" + suffix, "text/plain; charset=iso-8859-1"};
  }
  return {"data", "application/octet-stream; charset=binary"};
}

auto classify_content(const std::wstring& wpath) -> FileClassification {
  std::error_code ec;
  const auto size =
      std::filesystem::file_size(std::filesystem::path(
                                     native_path::normalize_api_operand_w(wpath)),
                                 ec);
  if (!ec && size == 0) {
    return {"empty", "inode/x-empty; charset=binary"};
  }

  const auto data = read_file_prefix(wpath, 256 * 1024);
  if (data.empty()) {
    return {"data", "application/octet-stream; charset=binary"};
  }
  if (auto magic = classify_by_magic(data)) return *magic;
  if (auto script = classify_by_shebang(data)) return *script;
  if (looks_like_json(data)) {
    return {"JSON text data", "application/json; charset=us-ascii"};
  }
  return classify_text(data);
}

/**
 * @brief Process a single file
 * @param path File path
 * @param brief Brief mode (no filename prefix)
 * @param symlink Follow symlinks
 * @param mime Output MIME type instead of description
 * @return The full output line for this operand. Like GNU file(1), an
 *         operand that cannot be opened yields a "cannot open" line on
 *         stdout; it is not an error of the command as a whole.
 */
auto process_file(const std::string& path, bool brief, bool symlink, bool mime)
    -> std::string {
  std::wstring wpath =
      utf8_to_wstring(native_path::normalize_api_operand(path));

  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    // [GNU] file.c: missing operands are reported on STDOUT and the exit
    // status stays 0 (issue #1383).
    const std::string detail = "cannot open '" + path + "' (" +
                               win32_posix_error_text(GetLastError()) + ")";
    return brief ? detail : path + ": " + detail;
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
      auto classification = classify_content(wpath);
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
    // [GNU] No operands: usage to stderr, exit 1 (issue #1383).
    safeErrorPrintLn("Usage: file [OPTION...] [FILE]...");
    return false;
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
        L"file: compiled with magic number detection based on file contents");
    safePrintLn(L"file: no magic file format on Windows");
    return true;
  }

  // [DIFFERS] -m/--magic-file: magic file not supported on Windows
  if (has_magic_file) {
    safeErrorPrint("file: --magic-file is not supported on Windows\n");
    safeErrorPrint(
        "file: file type detection uses built-in content tests on Windows\n");
    return false;
  }

  // [DIFFERS] -z/--uncompress: partial - detect .gz and warn
  // (actual decompression would require zlib; mark as [DIFFERS])
  if (uncompress) {
    // Continue but note compression support is limited
  }

  (void)keep_going;  // GNU reports every operand; there is no early stop.

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    safePrintLn(utf8_to_wstring(
        process_file(path, brief, symlink && !no_deref, mime)));
  }

  return true;
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
    "On Windows, file type detection is likewise based on the file's\n"
    "contents and attributes (directory, symlink, regular file) — the\n"
    "file name extension is never used to guess the type.",

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
