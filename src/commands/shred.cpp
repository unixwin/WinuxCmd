/*
 *  Copyright © 2026 WinuxCmd
 */
#include <wincrypt.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

auto constexpr SHRED_OPTIONS = std::array{
    // [GNU] -f, --force
    OPTION("-f", "--force", "ignore write-protection"),
    // [GNU] -n, --iterations
    OPTION("-n", "--iterations", "overwrite N times (default 3)", INT_TYPE),
    // [GNU] -u (equivalent to --remove)
    OPTION("-u", "", "truncate and remove file after overwriting"),
    // [GNU] -z, --zero
    OPTION("-z", "--zero",
           "add a final overwrite with zeros to hide shredding"),
    // [GNU] -v, --verbose
    OPTION("-v", "--verbose", "show progress"),
    // [GNU] --random-source
    // [DIFFERS] - random source file not supported; CryptGenRandom is used on
    // Windows
    OPTION("", "--random-source", "use FILE as the source of random data",
           STRING_TYPE),
    // [GNU] -s, --size
    OPTION("-s", "--size", "shred only BYTES bytes instead of the whole file",
           STRING_TYPE),
    // [GNU] -x, --exact
    OPTION("-x", "--exact", "do not round file size up to the next full block"),
    // [GNU] --remove[=HOW]: HOW can be 'unlink', 'wipe', or 'wipesync'
    OPTION("", "--remove",
           "truncate and remove FILE after overwriting; HOW can be 'unlink', "
           "'wipe', or 'wipesync'",
           OPTIONAL_STRING_TYPE),
};

REGISTER_COMMAND(
    shred,
    /* cmd_name */ "shred",
    /* cmd_synopsis */ "shred [OPTION]... FILE...",
    /* cmd_desc */
    "Overwrite the specified FILE(s) repeatedly to help prevent data recovery.",
    /* examples */ "shred -v -n 5 secret.txt\nshred -f -u -z keyfile.bin",
    /* see_also */ "rm(1)",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ SHRED_OPTIONS) {
  // [GNU] option values are validated before the operand check and
  // before any file is touched: `shred -n -1` reports the bad count
  // even with no (or a nonexistent) file operand.
  int passes = ctx.get<int>("-n", 3);
  if (passes < 0) {
    safeErrorPrintLn("shred: invalid number of passes: '" +
                     std::to_string(passes) + "'");
    return 1;
  }

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("shred: missing file operand");
    safeErrorPrintLn("Try 'shred --help' for more information.");
    return 1;
  }

  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  bool remove = ctx.get<bool>("-u", false) || ctx.has("--remove");
  // Parse --remove[=HOW]: [GNU] the bare default is wipesync (shred.c),
  // not unlink.
  std::string remove_how = "wipesync";
  if (ctx.has("--remove")) {
    auto how = ctx.get<std::string>("--remove", "");
    if (!how.empty()) {
      if (how != "unlink" && how != "wipe" && how != "wipesync") {
        safeErrorPrintLn("shred: invalid --remove argument: '" + how + "'");
        safeErrorPrintLn("Valid values are: unlink, wipe, wipesync");
        return 1;
      }
      remove_how = how;
    }
  }
  bool zero_fill = ctx.get<bool>("-z", false) || ctx.get<bool>("--zero", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool exact = ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  (void)ctx.has("--random-source");
  auto size_str = ctx.get<std::string>("-s", "");
  if (size_str.empty()) {
    size_str = ctx.get<std::string>("--size", "");
  }
  std::optional<LONGLONG> size_limit;
  if (ctx.has("-s") || ctx.has("--size")) {
    // [GNU] coreutils size grammar (xstrtoumax as used by shred -s):
    // a base-0 number (0x hex, leading-0 octal), then an optional
    // suffix: 'b' = 512, 'c' = 1, a power letter k/K/M/G/T/P/E/Z/Y
    // (only 'k' may be lowercase) for 1024^n, the letter plus 'B' for
    // 1000^n, the letter plus 'iB' for 1024^n, and a bare 'B' = 1024.
    // Anything else (including an explicitly empty value) is invalid.
    const std::string& s = size_str;
    // strtoull silently wraps a leading '-' into a huge magnitude;
    // the GNU unsigned grammar rejects a sign outright (plain
    // "invalid file size", not the overflow wording).
    {
      const char* p = s.c_str();
      while (*p == ' ' || *p == '\t') ++p;
      if (*p == '-') {
        safeErrorPrintLn("shred: invalid file size: '" + s + "'");
        return 1;
      }
    }
    char* endp = nullptr;
    const unsigned long long magnitude =
        std::strtoull(s.c_str(), &endp, 0);
    bool invalid = endp == s.c_str();
    unsigned long long value = magnitude;
    bool overflow =
        value > static_cast<unsigned long long>(
            std::numeric_limits<long long>::max());
    auto mul = [&](unsigned long long m) {
      if (m != 0 && value > ~0ULL / m) {
        overflow = true;
        return;
      }
      value *= m;
      if (value > static_cast<unsigned long long>(
            std::numeric_limits<long long>::max())) {
        overflow = true;
      }
    };
    if (!invalid && endp != nullptr && *endp != '\0') {
      const std::string suffix(endp);
      auto power_of = [](char c, int& power) {
        switch (c) {
          case 'k':
          case 'K':
            power = 1;
            return true;
          case 'M':
            power = 2;
            return true;
          case 'G':
            power = 3;
            return true;
          case 'T':
            power = 4;
            return true;
          case 'P':
            power = 5;
            return true;
          case 'E':
            power = 6;
            return true;
          case 'Z':
            power = 7;
            return true;
          case 'Y':
            power = 8;
            return true;
          default:
            return false;
        }
      };
      int power = 0;
      unsigned long long base = 0;
      if (suffix == "b") {
        mul(512);
      } else if (suffix == "c") {
        // x1
      } else if (suffix == "B") {
        mul(1024);
      } else if (suffix.size() == 1 && power_of(suffix[0], power)) {
        base = 1024;
      } else if (suffix.size() == 2 && suffix[1] == 'B' &&
                 power_of(suffix[0], power)) {
        base = 1000;
      } else if (suffix.size() == 3 && suffix[1] == 'i' && suffix[2] == 'B' &&
                 power_of(suffix[0], power)) {
        base = 1024;
      } else {
        invalid = true;
      }
      if (base != 0) {
        for (int i = 0; i < power; ++i) mul(base);
      }
    }
    if (invalid || overflow) {
      if (!invalid) {
        safeErrorPrintLn("shred: invalid file size: '" + s +
                         "': Value too large for defined data type");
      } else {
        safeErrorPrintLn("shred: invalid file size: '" + s + "'");
      }
      return 1;
    }
    size_limit = static_cast<LONGLONG>(value);
  }

  // Initialize CryptGenRandom
  HCRYPTPROV hProv = 0;
  BOOL crypt_ok = CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                                       CRYPT_VERIFYCONTEXT);

  int exit_code = 0;

  // Remove one already-overwritten file. For wipe/wipesync the file is
  // first renamed to a fresh unpredictable name in the same directory
  // and only THAT name is unlinked. The rename result must be checked
  // and a name this call did not just create must never be deleted:
  // the previous implementation renamed onto a compile-time constant
  // name (hex[(ri * 7 + 31) % 16] = "f6d4b2907e5c3a1"), ignored the
  // rename result, and deleted that path unconditionally — when an
  // unrelated file already had the constant name, the rename failed,
  // the unrelated file was deleted, and the shred target survived
  // (silent data loss, exit 0).
  auto remove_one = [&](const std::wstring& api_path,
                        const std::string& display) -> bool {
    if (remove_how == "unlink") {
      if (DeleteFileW(api_path.c_str())) return true;
      safeErrorPrintLn("shred: cannot remove '" + display + "'");
      return false;
    }
    std::wstring dir_part;
    const auto sep = api_path.find_last_of(L"\\/");
    if (sep != std::wstring::npos) dir_part = api_path.substr(0, sep + 1);
    for (int attempt = 0; attempt < 100; ++attempt) {
      unsigned char bytes[8] = {};
      if (crypt_ok) {
        CryptGenRandom(hProv, static_cast<DWORD>(sizeof(bytes)), bytes);
      } else {
        // Fallback entropy when no CSP is available: time, pid and the
        // attempt counter through an LCG.
        unsigned long long seed =
            static_cast<unsigned long long>(std::time(nullptr)) ^
            (static_cast<unsigned long long>(GetCurrentProcessId()) << 16) ^
            (static_cast<unsigned long long>(attempt + 1) *
             0x9E3779B97F4A7C15ULL);
        for (int i = 0; i < 8; ++i) {
          seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
          bytes[i] = static_cast<unsigned char>(seed >> 56);
        }
      }
      static constexpr char hexd[] = "0123456789abcdef";
      std::wstring candidate = dir_part;
      for (unsigned char b : bytes) {
        candidate += static_cast<wchar_t>(hexd[b >> 4]);
        candidate += static_cast<wchar_t>(hexd[b & 0xF]);
      }
      // Never rename onto (or delete) a path that already exists.
      if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        continue;
      }
      if (!MoveFileW(api_path.c_str(), candidate.c_str())) {
        const DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS) {
          continue;
        }
        safeErrorPrintLn("shred: cannot remove '" + display + "'");
        return false;
      }
      if (remove_how == "wipesync" && !dir_part.empty()) {
        // Sync the parent directory to ensure the rename is durable.
        std::wstring parent_dir = dir_part;
        while (parent_dir.size() > 1 &&
               (parent_dir.back() == L'\\' || parent_dir.back() == L'/')) {
          parent_dir.pop_back();
        }
        HANDLE hDir = CreateFileW(
            parent_dir.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (hDir != INVALID_HANDLE_VALUE) {
          FlushFileBuffers(hDir);
          CloseHandle(hDir);
        }
      }
      if (!DeleteFileW(candidate.c_str())) {
        safeErrorPrintLn("shred: cannot remove '" + display + "'");
        return false;
      }
      return true;
    }
    safeErrorPrintLn("shred: cannot remove '" + display + "'");
    return false;
  };

  for (const auto& filename : ctx.positionals) {
    // #1240: the operand may use any accepted POSIX drive spelling
    // ("/e/f", "/cygdrive/e/f", "/mnt/e/f"). Handing that raw text to
    // CreateFileW / GetFileAttributesW / DeleteFileW makes Win32 resolve a
    // leading "/e" against the CURRENT drive, so from a C: cwd
    // `shred /e/Data/secret` silently overwrote "C:\e\Data\secret" whenever
    // that shadow existed (rc 0, wrong target destroyed) and reported
    // "cannot open" for a perfectly good E:\ file otherwise. A
    // data-destroying command must fold the dialect through the shared
    // make_api_path_operand boundary before it touches the filesystem -
    // before the native-glob pass too, so a "/e/*.log" pattern expands
    // against the operand drive instead of the cwd drive. The typed
    // spelling stays in `filename` for every message, like MSYS.
    const auto operand = native_path::make_api_path_operand(filename);

    // One shred job per matched path: the API form for every Win32 call and
    // the text the messages quote (the operand as typed, or the path a
    // glob matched - the shell would have substituted that in argv).
    struct ShredJob {
      std::wstring api;
      std::string display;
    };
    std::vector<ShredJob> jobs;
    if (contains_wildcard(operand.normalized)) {
      auto glob_result = glob_expand(operand.normalized);
      if (glob_result.expanded) {
        for (const auto& f : glob_result.files) {
          auto matched = native_path::make_api_path_operand_w(f);
          jobs.push_back({std::move(matched.extended),
                          wstring_to_utf8(matched.normalized)});
        }
      }
    }
    if (jobs.empty()) {
      // No glob match (or no pattern): the operand itself, with the
      // unmatched pattern kept literal so diagnostics stay useful.
      jobs.push_back({operand.extended, std::string(filename)});
    }

    for (const auto& job : jobs) {
      const std::string& exp = job.display;
      // [GNU] "-" wipes standard output instead of a named file
      // (shred.c: wipefd(STDOUT_FILENO, ...), uutils #12288). It is only
      // valid when stdout is a regular file; pipes/consoles fail with
      // "invalid file type".
      HANDLE hFile = INVALID_HANDLE_VALUE;
      bool is_stdout = exp == "-";
      std::wstring wfilename;
      if (is_stdout) {
        hFile = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE ||
            GetFileType(hFile) != FILE_TYPE_DISK) {
          safeErrorPrintLn("shred: -: invalid file type");
          exit_code = 1;
          continue;
        }
      } else {
        wfilename = job.api;
        hFile =
            CreateFileW(wfilename.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      }

      if (hFile == INVALID_HANDLE_VALUE && force && !is_stdout) {
        // If --force, try removing the read-only attribute and retry
        DWORD attrs = GetFileAttributesW(wfilename.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_READONLY)) {
          SetFileAttributesW(wfilename.c_str(),
                             attrs & ~FILE_ATTRIBUTE_READONLY);
          hFile = CreateFileW(wfilename.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
        }
      }

      if (hFile == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn("shred: cannot open '" + exp + "'");
        exit_code = 1;
        continue;
      }

      LARGE_INTEGER fileSize;
      GetFileSizeEx(hFile, &fileSize);
      LONGLONG size = fileSize.QuadPart;

      if (size_limit) {
        // [GNU] -s N shreds exactly N bytes: the file grows when N
        // exceeds its current size, and the block round-up below does
        // not apply to an explicit size (a 5-byte file with -s 100
        // ends up exactly 100 bytes, not one rounded block).
        size = *size_limit;
      } else if (!exact && size > 0) {
        // Round up to block boundary unless --exact is specified
        constexpr LONGLONG BLOCK_SIZE = 512;
        size = ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
      }

      if (size == 0) {
        if (!is_stdout) {
          CloseHandle(hFile);
          if (remove && !remove_one(wfilename, exp)) {
            exit_code = 1;
          }
        }
        continue;
      }

      if (verbose) {
        safePrint("shred: '" + exp + "': passing 1.." +
                  std::to_string(passes + (zero_fill ? 1 : 0)) + "\n");
      }

      // Overwrite file multiple times with random data
      for (int i = 0; i < passes; ++i) {
        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);
        constexpr DWORD kBufferSize = 64 * 1024;
        std::vector<char> buffer(kBufferSize);
        LONGLONG remaining = size;
        LONGLONG offset = 0;
        while (remaining > 0) {
          DWORD count =
              static_cast<DWORD>(std::min<LONGLONG>(remaining, kBufferSize));
          if (crypt_ok) {
            if (!CryptGenRandom(hProv, count,
                                reinterpret_cast<BYTE*>(buffer.data()))) {
              if (!is_stdout) CloseHandle(hFile);
              exit_code = 1;
              break;
            }
          } else {
            for (DWORD j = 0; j < count; ++j) {
              buffer[j] = static_cast<char>(
                  (i + offset + j + static_cast<int>(std::time(nullptr))) %
                  256);
            }
          }
          DWORD bytes_written = 0;
          if (!WriteFile(hFile, buffer.data(), count, &bytes_written,
                         nullptr) ||
              bytes_written != count) {
            exit_code = 1;
            break;
          }
          offset += count;
          remaining -= count;
        }
        FlushFileBuffers(hFile);

        if (verbose) {
          safePrint("shred: '" + exp + "': pass " + std::to_string(i + 1) +
                    "/" + std::to_string(passes + (zero_fill ? 1 : 0)) + "\n");
        }
      }

      // Final zero overwrite if requested
      if (zero_fill) {
        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);
        constexpr DWORD kBufferSize = 64 * 1024;
        std::vector<char> zeros(kBufferSize, 0);
        LONGLONG remaining = size;
        while (remaining > 0) {
          DWORD count =
              static_cast<DWORD>(std::min<LONGLONG>(remaining, kBufferSize));
          DWORD bytes_written = 0;
          if (!WriteFile(hFile, zeros.data(), count, &bytes_written, nullptr) ||
              bytes_written != count) {
            exit_code = 1;
            break;
          }
          remaining -= count;
        }
        FlushFileBuffers(hFile);

        if (verbose) {
          safePrint("shred: '" + exp + "': pass " + std::to_string(passes + 1) +
                    "/" + std::to_string(passes + 1) + " (zero)\n");
        }
      }

      if (!is_stdout) {
        CloseHandle(hFile);
      }

      // Remove file if requested (never removes stdout for "-")
      if (remove && !is_stdout) {
        if (!remove_one(wfilename, exp)) {
          exit_code = 1;
        } else if (verbose) {
          safePrint("shred: '" + exp + "': removed (" + remove_how + ")\n");
        }
      }
    }
  }

  if (crypt_ok) {
    CryptReleaseContext(hProv, 0);
  }

  return exit_code;
}
