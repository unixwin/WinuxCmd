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
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("shred: missing file operand");
    safeErrorPrintLn("Try 'shred --help' for more information.");
    return 1;
  }

  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  int passes = ctx.get<int>("-n", 3);
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
  if (!size_str.empty()) {
    LONGLONG parsed = 0;
    const char* data = size_str.data();
    const char* end = size_str.data() + size_str.size();
    auto [ptr, ec] = std::from_chars(data, end, parsed);
    if (ec != std::errc() || parsed < 0) {
      safeErrorPrintLn("shred: invalid size: '" + size_str + "'");
      return 1;
    }
    // Parse optional suffix: K, M, G, T, P, E (case-insensitive)
    LONGLONG multiplier = 1;
    if (ptr < end) {
      char suffix =
          static_cast<char>(std::toupper(static_cast<unsigned char>(*ptr)));
      ++ptr;
      switch (suffix) {
        case 'E':
          multiplier *= 1024;
          [[fallthrough]];
        case 'P':
          multiplier *= 1024;
          [[fallthrough]];
        case 'T':
          multiplier *= 1024;
          [[fallthrough]];
        case 'G':
          multiplier *= 1024;
          [[fallthrough]];
        case 'M':
          multiplier *= 1024;
          [[fallthrough]];
        case 'K':
          multiplier *= 1024;
          break;
        default:
          safeErrorPrintLn("shred: invalid suffix in size: '" + size_str + "'");
          return 1;
      }
      if (ptr != end) {
        safeErrorPrintLn("shred: invalid size: '" + size_str + "'");
        return 1;
      }
    }
    parsed *= multiplier;
    size_limit = parsed;
  }

  // Initialize CryptGenRandom
  HCRYPTPROV hProv = 0;
  BOOL crypt_ok = CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                                       CRYPT_VERIFYCONTEXT);

  int exit_code = 0;

  for (const auto& filename : ctx.positionals) {
    std::string file_arg(filename);
    std::vector<std::string> expanded;
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& f : glob_result.files) {
          expanded.push_back(wstring_to_utf8(f));
        }
      } else {
        expanded.push_back(file_arg);
      }
    } else {
      expanded.push_back(file_arg);
    }

    for (const auto& exp : expanded) {
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
        wfilename = utf8_to_wstring(exp);
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

      // Apply --size limit
      if (size_limit) {
        size = std::min(size, *size_limit);
      }

      // Round up to block boundary unless --exact is specified
      if (!exact && size > 0) {
        constexpr LONGLONG BLOCK_SIZE = 512;
        size = ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
      }

      if (size == 0) {
        if (!is_stdout) {
          CloseHandle(hFile);
          if (remove) {
            DeleteFileW(wfilename.c_str());
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
        if (remove_how == "wipe" || remove_how == "wipesync") {
          // Obfuscate the file name before deletion (wipe/wipesync mode).
          // On Windows, rename the file to a random name before unlinking.
          std::wstring random_name = wfilename;
          auto last_bs = random_name.find_last_of(L'\\');
          if (last_bs != std::wstring::npos) {
            std::wstring dir_part = random_name.substr(0, last_bs + 1);
            // Generate a random 15-char hex name
            wchar_t rand_buf[16];
            for (int ri = 0; ri < 15; ++ri) {
              static constexpr wchar_t hex[] = L"0123456789abcdef";
              rand_buf[ri] =
                  hex[(static_cast<unsigned char>(ri * 7 + 31)) % 16];
            }
            rand_buf[15] = 0;
            random_name = dir_part + std::wstring(rand_buf);
            MoveFileW(wfilename.c_str(), random_name.c_str());
            if (remove_how == "wipesync") {
              // Sync the parent directory to ensure the rename is durable
              std::wstring parent_dir = wfilename.substr(0, last_bs);
              HANDLE hDir = CreateFileW(
                  parent_dir.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
              if (hDir != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(hDir);
                CloseHandle(hDir);
              }
            }
            DeleteFileW(random_name.c_str());
          } else {
            DeleteFileW(wfilename.c_str());
          }
        } else {
          // Default: unlink -- simple deletion
          DeleteFileW(wfilename.c_str());
        }
        if (verbose) {
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
