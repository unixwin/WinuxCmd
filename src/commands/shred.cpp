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
    // [GNU] -n, --iterations (STRING: the value is a uintmax parsed
    // by the handler, not the framework's 32-bit int — #1462 S7)
    OPTION("-n", "--iterations", "overwrite N times (default 3)",
           STRING_TYPE),
    // [GNU] -u (equivalent to --remove)
    OPTION("-u", "", "truncate and remove file after overwriting"),
    // [GNU] -z, --zero
    OPTION("-z", "--zero",
           "add a final overwrite with zeros to hide shredding"),
    // [GNU] -v, --verbose
    OPTION("-v", "--verbose", "show progress"),
    // [GNU] --random-source: the file supplies the random bytes
    // (implemented in the handler; the CSPRNG is the default source)
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
  // [GNU] Option values are validated during getopt, BEFORE the
  // missing-operand check (shred.c:1184-1222 vs 1242-1246): a bad
  // -n/-s/--remove reports itself even with no file operand (S6).
  // This ordering was inverted for -s and --remove.

  // [GNU] -n is xdectoumax (shred.c:1192-1196): uintmax range, a
  // leading '+' is fine, and a negative value wraps and reports as
  // overflow. The framework's 32-bit int rejected all three (S7).
  unsigned long long passes = 3;
  {
    std::string n_value = ctx.get<std::string>("--iterations", "");
    if (n_value.empty()) n_value = ctx.get<std::string>("-n", "");
    if (n_value.empty()) {
      // Belt and braces: take the token after -n/--iterations
      // verbatim from the raw argv when the typed lookup is empty.
      for (size_t i = 0; i + 1 < ctx.raw_args.size(); ++i) {
        if (ctx.raw_args[i] == "-n" || ctx.raw_args[i] == "--iterations") {
          n_value = ctx.raw_args[i + 1];
          break;
        }
      }
    }
    if (!n_value.empty()) {
      const std::string& v = n_value;
      size_t pos = 0;
      while (pos < v.size() && (v[pos] == ' ' || v[pos] == '\t')) ++pos;
      bool overflow = false;
      bool invalid = false;
      bool negative = false;
      if (pos < v.size() && v[pos] == '-') {
        // [GNU] A negative count is diagnosed WITHOUT the overflow
        // suffix; the suffix is for positive overflow only.
        negative = true;
        overflow = true;
      } else {
        if (pos < v.size() && v[pos] == '+') ++pos;
        const size_t digits_begin = pos;
        unsigned long long value = 0;
        while (pos < v.size() && v[pos] >= '0' && v[pos] <= '9') {
          const unsigned digit = static_cast<unsigned>(v[pos] - '0');
          if (value > (~0ULL - digit) / 10ULL) {
            overflow = true;
          } else {
            value = value * 10ULL + digit;
          }
          ++pos;
        }
        if (pos == digits_begin || pos != v.size()) {
          invalid = true;
        } else if (value >
                   static_cast<unsigned long long>(
                       std::numeric_limits<unsigned long long>::max() /
                       sizeof(int))) {
          overflow = true;
        } else {
          passes = value;
        }
      }
      if (invalid || overflow) {
        safeErrorPrintLn("shred: invalid number of passes: '" + v + "'" +
                         (overflow && !negative
                              ? ": Value too large for defined data type"
                              : ""));
        return 1;
      }
    }
  }

  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  bool remove = ctx.get<bool>("-u", false) || ctx.has("--remove");
  // Parse --remove[=HOW]: [GNU] the bare default is wipesync
  // (shred.c), not unlink. HOW is argmatch-resolved (shred.c:
  // 1208-1211): unambiguous prefixes work, and 'w' is ambiguous
  // between wipe and wipesync (S5). Exact words used to be required,
  // with an invented diagnostic.
  std::string remove_how = "wipesync";
  if (ctx.has("--remove")) {
    auto how = ctx.get<std::string>("--remove", "");
    if (!how.empty()) {
      static constexpr std::string_view remove_words[] = {"unlink", "wipe",
                                                          "wipesync"};
      std::string_view matched;
      size_t matches = 0;
      for (const std::string_view word : remove_words) {
        if (word == how) {
          matched = word;
          matches = 1;
          break;
        }
        if (word.starts_with(how)) {
          matched = word;
          ++matches;
        }
      }
      if (matches != 1) {
        safeErrorPrintLn(std::string("shred: ") +
                         (matches == 0 ? "invalid argument '"
                                       : "ambiguous argument '") +
                         how + "' for '--remove'");
        safeErrorPrintLn("Valid arguments are:");
        safeErrorPrintLn("  - 'unlink'");
        safeErrorPrintLn("  - 'wipe'");
        safeErrorPrintLn("  - 'wipesync'");
        safeErrorPrintLn("Try 'shred --help' for more information.");
        return 1;
      }
      remove_how = std::string(matched);
    }
  }
  bool zero_fill = ctx.get<bool>("-z", false) || ctx.get<bool>("--zero", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool exact = ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  auto size_str = ctx.get<std::string>("-s", "");
  if (size_str.empty()) {
    size_str = ctx.get<std::string>("--size", "");
  }
  std::optional<LONGLONG> size_limit;
  if (ctx.has("-s") || ctx.has("--size")) {
    // [GNU] coreutils size grammar (xstrtoumax as used by shred -s):
    // a base-0 number (0x hex, leading-0 octal), then an optional
    // suffix: 'b' = 512, 'c' = 1, a power letter k/K/M/G/T/P/E/Z/Y/R/Q
    // (only 'k' may be lowercase) for 1024^n, the letter plus 'B' for
    // 1000^n, the letter plus 'iB' for 1024^n, and a bare 'B' = 1024.
    // A bare suffix with no digits means 1x the suffix. Anything else
    // (including an explicitly empty value) is invalid.
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
    bool bare_suffix = endp == s.c_str();
    bool invalid = false;
    unsigned long long value = bare_suffix ? 1 : magnitude;
    if (bare_suffix) {
      // No digits at all: only a lone suffix is salvageable (GNU
      // treats it as 1x); anything else is invalid (S4).
      endp = const_cast<char*>(s.c_str());
      if (s.empty()) invalid = true;
    }
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
          // [GNU] The suffix string is "cbBkKMGTPEZYRQ0"
          // (shred.c:1214-1215): R and Q were missing here, so 0R/0Q
          // failed and nonzero values lost the overflow wording.
          case 'R':
            power = 9;
            return true;
          case 'Q':
            power = 10;
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
      } else if (suffix.size() == 2 && suffix[1] == 'D' &&
                 power_of(suffix[0], power)) {
        // [GNU] The obsolescent second suffix admitted by the '0'
        // in the suffix string: letter+'D' = 1000^n (xstrtol.c).
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

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("shred: missing file operand");
    safeErrorPrintLn("Try 'shred --help' for more information.");
    return 1;
  }

  // Initialize CryptGenRandom
  HCRYPTPROV hProv = 0;
  BOOL crypt_ok = CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                                       CRYPT_VERIFYCONTEXT);

  // [GNU] --random-source (shred.c:1199-1202 + randread setup): the
  // named file supplies the random bytes; two DIFFERENT sources are
  // fatal, and an unopenable source aborts before any file is
  // shredded. The option was declared with a [DIFFERS] note and
  // then discarded (S1).
  std::string random_source;
  for (const auto& occurrence :
       ctx.string_occurrences({"--random-source"})) {
    if (!random_source.empty() && random_source != occurrence.value) {
      safeErrorPrintLn("shred: multiple random sources specified");
      if (crypt_ok) CryptReleaseContext(hProv, 0);
      return 1;
    }
    random_source = occurrence.value;
  }
  HANDLE source_handle = INVALID_HANDLE_VALUE;
  if (!random_source.empty()) {
    const auto source_operand =
        native_path::make_api_path_operand(random_source);
    source_handle = CreateFileW(
        source_operand.extended.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (source_handle == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("shred: " + random_source +
                       ": " + win32_posix_error_text(GetLastError()));
      if (crypt_ok) CryptReleaseContext(hProv, 0);
      return 1;
    }
  }

  // Fill one buffer with shred randomness: the named source when
  // given, else the CSPRNG, else the weak time-based fallback.
  auto fill_random = [&](BYTE* bytes, DWORD count) -> bool {
    if (source_handle != INVALID_HANDLE_VALUE) {
      DWORD done = 0;
      while (done < count) {
        DWORD read = 0;
        if (!ReadFile(source_handle, bytes + done, count - done, &read,
                      nullptr) ||
            read == 0) {
          return false;
        }
        done += read;
      }
      return true;
    }
    if (crypt_ok) {
      return CryptGenRandom(hProv, count, bytes) != FALSE;
    }
    for (DWORD j = 0; j < count; ++j) {
      bytes[j] = static_cast<BYTE>((std::time(nullptr) + j) % 256);
    }
    return true;
  };

  int exit_code = 0;

  // Sync a directory so a rename or unlink inside it is durable
  // (the wipesync half of --remove).
  auto sync_parent_dir = [](const std::wstring& dir_part) {
    if (dir_part.empty()) {
      return;
    }
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
  };

  // [GNU] Removal (shred.c do_wipefd + wipename): the caller has
  // already truncated the file to 0 through its open handle
  // (shred.c:938-947), so a surviving hard link sees an empty file.
  // 'wipe'/'wipesync' obfuscate the name first: one rename per
  // basename length, from the full length down to 1, names drawn in
  // order from the nameset odometer starting at all-'0's
  // (shred.c:979, 1053-1085); 'wipesync' syncs the directory after
  // each rename. A failure reads "<name>: failed to remove:
  // <reason>" (shred.c:1087-1090). The previous implementation did
  // a single rename to a random 16-hex name, never truncated, and
  // reported a reason-less 'cannot remove'. One lesson from its
  // history is kept: never delete a name this run did not create —
  // odometer names that already exist are skipped by advancing the
  // odometer instead of replacing the stranger's file.
  auto remove_one = [&](const std::wstring& api_path,
                        const std::string& display) -> bool {
    auto fail_remove = [&](DWORD err) {
      safeErrorPrintLn("shred: " + display + ": failed to remove: " +
                       win32_posix_error_text(err));
      return false;
    };
    if (remove_how == "unlink") {
      if (DeleteFileW(api_path.c_str())) {
        return true;
      }
      return fail_remove(GetLastError());
    }

    std::wstring dir_part;
    std::wstring base_name = api_path;
    const auto sep = api_path.find_last_of(L"\\/");
    if (sep != std::wstring::npos) {
      dir_part = api_path.substr(0, sep + 1);
      base_name = api_path.substr(sep + 1);
    }
    if (base_name.empty()) {
      return fail_remove(ERROR_INVALID_NAME);
    }
    if (verbose) {
      safeErrorPrintLn("shred: " + display + ": removing");
    }

    static constexpr wchar_t kNameset[] =
        L"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr size_t kNamesetSize = std::size(kNameset) - 1;
    std::vector<size_t> odometer(base_name.size(), 0);
    auto advance_odometer = [&]() {
      for (size_t i = odometer.size(); i-- > 0;) {
        if (odometer[i] + 1 < kNamesetSize) {
          ++odometer[i];
          return;
        }
        odometer[i] = 0;
      }
    };

    std::wstring current = api_path;
    for (size_t len = base_name.size(); len >= 1; --len) {
      std::wstring target;
      for (int guard = 0; guard < 4096; ++guard) {
        target = dir_part;
        for (size_t k = 0; k < len; ++k) {
          target += kNameset[odometer[k]];
        }
        if (GetFileAttributesW(target.c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
          break;
        }
        advance_odometer();
        target.clear();
      }
      if (target.empty()) {
        return fail_remove(ERROR_FILE_EXISTS);
      }
      if (!MoveFileW(current.c_str(), target.c_str())) {
        return fail_remove(GetLastError());
      }
      if (verbose) {
        safeErrorPrintLn("shred: " + display +
                         ": renamed to " + wstring_to_utf8(target));
      }
      if (remove_how == "wipesync") {
        sync_parent_dir(dir_part);
      }
      current = target;
      advance_odometer();
    }
    if (!DeleteFileW(current.c_str())) {
      return fail_remove(GetLastError());
    }
    if (verbose) {
      safeErrorPrintLn("shred: " + display + ": removed");
    }
    return true;
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
      DWORD open_error = 0;
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
        // [GNU] Opened write-only (shred.c:1127: O_WRONLY). A file
        // the user may write but not read used to fail here (S9).
        hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
          open_error = GetLastError();
        }
      }

      if (hFile == INVALID_HANDLE_VALUE && force && !is_stdout) {
        // If --force, try removing the read-only attribute and retry
        DWORD attrs = GetFileAttributesW(wfilename.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_READONLY)) {
          SetFileAttributesW(wfilename.c_str(),
                             attrs & ~FILE_ATTRIBUTE_READONLY);
          hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
          if (hFile == INVALID_HANDLE_VALUE) {
            open_error = GetLastError();
          }
        }
      }

      if (hFile == INVALID_HANDLE_VALUE) {
        // [GNU] "shred: <name>: failed to open for writing: <reason>"
        // (shred.c:1133). Every failure used to collapse to a
        // reason-less 'cannot open' (S8).
        std::string reason = win32_posix_error_text(open_error);
        const DWORD attrs_now = GetFileAttributesW(wfilename.c_str());
        if (attrs_now != INVALID_FILE_ATTRIBUTES &&
            (attrs_now & FILE_ATTRIBUTE_DIRECTORY)) {
          reason = "Is a directory";
        }
        safeErrorPrintLn("shred: " + exp +
                         ": failed to open for writing: " + reason);
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
        // [GNU] Round up to the file's block size (ST_BLKSIZE,
        // shred.c:867-875) — the volume cluster size here. A fixed
        // 512 under-covered the slack on 4K-cluster volumes (S3).
        LONGLONG block = 512;
        if (!is_stdout) {
          wchar_t volume_root[MAX_PATH];
          if (GetVolumePathNameW(wfilename.c_str(), volume_root, MAX_PATH)) {
            DWORD sectors_per_cluster = 0;
            DWORD bytes_per_sector = 0;
            DWORD unused = 0;
            if (GetDiskFreeSpaceW(volume_root, &sectors_per_cluster,
                                  &bytes_per_sector, &unused, &unused) &&
                sectors_per_cluster > 0 && bytes_per_sector > 0) {
              block = static_cast<LONGLONG>(sectors_per_cluster) *
                      bytes_per_sector;
            }
          }
        }
        size = ((size + block - 1) / block) * block;
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

      const unsigned long long total_passes =
          passes + (zero_fill ? 1 : 0);
      // [GNU] Removal runs only when the wipe succeeded (shred.c:
      // 1144: 'if (ok && flags->remove_file)'). Pass failures used
      // to be deleted over anyway (S2).
      bool wipe_ok = true;

      // Overwrite the file `passes` times with random data
      for (unsigned long long i = 0; wipe_ok && i < passes; ++i) {
        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);
        constexpr DWORD kBufferSize = 64 * 1024;
        std::vector<char> buffer(kBufferSize);
        LONGLONG remaining = size;
        while (remaining > 0) {
          DWORD count =
              static_cast<DWORD>(std::min<LONGLONG>(remaining, kBufferSize));
          if (!fill_random(reinterpret_cast<BYTE*>(buffer.data()), count)) {
            wipe_ok = false;
            exit_code = 1;
            break;
          }
          DWORD bytes_written = 0;
          if (!WriteFile(hFile, buffer.data(), count, &bytes_written,
                         nullptr) ||
              bytes_written != count) {
            wipe_ok = false;
            exit_code = 1;
            break;
          }
          remaining -= count;
        }
        if (wipe_ok) {
          FlushFileBuffers(hFile);
          if (verbose) {
            // [GNU] Verbose goes to STDERR in GNU's shape:
            // "<name>: pass <i>/<n> (random)..." (shred.c:576).
            // Invented text on stdout was used before (S10).
            safeErrorPrintLn("shred: " + exp + ": pass " +
                             std::to_string(i + 1) + "/" +
                             std::to_string(total_passes) + " (random)...");
          }
        }
      }

      // Final zero overwrite if requested
      if (zero_fill && wipe_ok) {
        LARGE_INTEGER li = {0};
        SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN);
        constexpr DWORD kBufferSize = 64 * 1024;
        std::vector<char> zeros(kBufferSize, 0);
        LONGLONG remaining = size;
        while (remaining > 0) {
          DWORD count =
              static_cast<DWORD>(std::min<LONGLONG>(remaining, kBufferSize));
          DWORD bytes_written = 0;
          if (!WriteFile(hFile, zeros.data(), count, &bytes_written,
                         nullptr) ||
              bytes_written != count) {
            wipe_ok = false;
            exit_code = 1;
            break;
          }
          remaining -= count;
        }
        if (wipe_ok) {
          FlushFileBuffers(hFile);
          if (verbose) {
            safeErrorPrintLn("shred: " + exp + ": pass " +
                             std::to_string(passes + 1) + "/" +
                             std::to_string(total_passes) + " (zero)...");
          }
        }
      }

      if (is_stdout) {
        // [GNU] -u on '-' truncates the stdout file (shred.c
        // do_wipefd); there is nothing to unlink.
        if (remove && wipe_ok) {
          LARGE_INTEGER zero_pos = {0};
          SetFilePointerEx(hFile, zero_pos, nullptr, FILE_BEGIN);
          SetEndOfFile(hFile);
        }
      } else {
        if (remove && wipe_ok) {
          // [GNU] Truncate to 0 through the open handle before
          // unlinking (shred.c:938-947), so a second hard link sees
          // an empty file, not the full shredded content.
          LARGE_INTEGER zero_pos = {0};
          SetFilePointerEx(hFile, zero_pos, nullptr, FILE_BEGIN);
          SetEndOfFile(hFile);
        }
        CloseHandle(hFile);
        if (remove && wipe_ok && !remove_one(wfilename, exp)) {
          exit_code = 1;
        }
      }
    }
  }

  if (source_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(source_handle);
  }
  if (crypt_ok) {
    CryptReleaseContext(hProv, 0);
  }

  return exit_code;
}
