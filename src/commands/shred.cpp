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
    OPTION("-f", "--force", "ignore write-protection"),
    OPTION("-n", "--iterations", "overwrite N times (default 3)", INT_TYPE),
    OPTION("-u", "--remove", "truncate and remove file after overwriting"),
    OPTION("-z", "--zero",
           "add a final overwrite with zeros to hide shredding"),
    OPTION("-v", "--verbose", "show progress"),
};

REGISTER_COMMAND(
    shred,
    /* cmd_name */ "shred",
    /* cmd_synopsis */ "shred [OPTION]... FILE...",
    /* cmd_desc */
    "Overwrite the specified FILE(s) repeatedly to help prevent\n"
    "data recovery.",
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
  bool remove = ctx.get<bool>("-u", false) || ctx.get<bool>("--remove", false);
  bool zero_fill = ctx.get<bool>("-z", false) || ctx.get<bool>("--zero", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

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
      std::wstring wfilename = utf8_to_wstring(exp);
      HANDLE hFile =
          CreateFileW(wfilename.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

      if (hFile == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn("shred: cannot open '" + exp + "'");
        exit_code = 1;
        continue;
      }

      LARGE_INTEGER fileSize;
      GetFileSizeEx(hFile, &fileSize);
      LONGLONG size = fileSize.QuadPart;

      if (size == 0) {
        CloseHandle(hFile);
        if (remove) {
          DeleteFileW(wfilename.c_str());
        }
        continue;
      }

      if (verbose) {
        safePrint("shred: '" + exp + "': passing 1.." +
                  std::to_string(passes + (zero_fill ? 1 : 0)) + "\n");
      }

      // Overwrite file multiple times with random data
      for (int i = 0; i < passes; ++i) {
        std::vector<char> buffer(size);

        // Generate random data using CryptGenRandom
        if (crypt_ok) {
          CryptGenRandom(hProv, static_cast<DWORD>(size),
                         reinterpret_cast<BYTE*>(buffer.data()));
        } else {
          // Fallback: use pattern (not truly random, but better than nothing)
          for (LONGLONG j = 0; j < size; ++j) {
            buffer[j] = static_cast<char>(
                (i + j + static_cast<int>(std::time(nullptr))) % 256);
          }
        }

        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);

        DWORD bytesWritten;
        WriteFile(hFile, buffer.data(), static_cast<DWORD>(size), &bytesWritten,
                  nullptr);
        FlushFileBuffers(hFile);

        if (verbose) {
          safePrint("shred: '" + exp + "': pass " + std::to_string(i + 1) +
                    "/" + std::to_string(passes + (zero_fill ? 1 : 0)) + "\n");
        }
      }

      // Final zero overwrite if requested
      if (zero_fill) {
        std::vector<char> zeros(size, 0);

        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);

        DWORD bytesWritten;
        WriteFile(hFile, zeros.data(), static_cast<DWORD>(size), &bytesWritten,
                  nullptr);
        FlushFileBuffers(hFile);

        if (verbose) {
          safePrint("shred: '" + exp + "': pass " + std::to_string(passes + 1) +
                    "/" + std::to_string(passes + 1) + " (zero)\n");
        }
      }

      CloseHandle(hFile);

      // Remove file if requested
      if (remove) {
        DeleteFileW(wfilename.c_str());
        if (verbose) {
          safePrint("shred: '" + exp + "': removed\n");
        }
      }
    }
  }

  if (crypt_ok) {
    CryptReleaseContext(hProv, 0);
  }

  return exit_code;
}
