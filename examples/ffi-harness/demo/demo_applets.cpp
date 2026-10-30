// Demo applet logic for the FFI harness test doubles (see demo_applets.h).
// Raw Win32 file IO only, binary, no CRT stdio — byte behaviour is then
// identical between the in-process (DLL) and spawn (exe) doubles.
#include "demo_applets.h"

#include <windows.h>

namespace {

bool write_all(HANDLE h, const void *data, DWORD size) {
  const auto *p = static_cast<const unsigned char *>(data);
  while (size > 0) {
    DWORD written = 0;
    if (!WriteFile(h, p, size, &written, nullptr) || written == 0) return false;
    p += written;
    size -= written;
  }
  return true;
}

void diag_line(const std::wstring &msg) {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  if (!h || h == INVALID_HANDLE_VALUE) return;
  std::string narrow;
  for (wchar_t c : msg) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
  narrow.push_back('\n');
  DWORD written = 0;
  WriteFile(h, narrow.data(), static_cast<DWORD>(narrow.size()), &written,
            nullptr);
}

int copy_stream(HANDLE in, HANDLE out) {
  unsigned char buf[8192];
  while (true) {
    DWORD got = 0;
    if (!ReadFile(in, buf, sizeof(buf), &got, nullptr)) return 1;
    if (got == 0) return 0;
    if (!write_all(out, buf, got)) return 1;
  }
}

}  // namespace

int demo_cat(const std::vector<std::wstring> &args) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (args.empty()) {
    return copy_stream(GetStdHandle(STD_INPUT_HANDLE), out);
  }
  int rc = 0;
  for (const auto &path : args) {
    if (path == L"-") {
      if (copy_stream(GetStdHandle(STD_INPUT_HANDLE), out) != 0) rc = 1;
      continue;
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      diag_line(L"cat: cannot open file");
      rc = 1;
      continue;
    }
    if (copy_stream(h, out) != 0) rc = 1;
    CloseHandle(h);
  }
  return rc;
}

int demo_cp(const std::vector<std::wstring> &args) {
  if (args.size() != 2) {
    diag_line(L"cp: expected exactly SOURCE DEST");
    return 1;
  }
  if (!CopyFileW(args[0].c_str(), args[1].c_str(), FALSE)) {
    diag_line(L"cp: copy failed");
    return 1;
  }
  return 0;
}

int demo_rm(const std::vector<std::wstring> &args) {
  bool force = false;
  int rc = 0;
  for (const auto &arg : args) {
    if (arg == L"-f" || arg == L"--force") {
      force = true;
      continue;
    }
    if (!arg.empty() && arg[0] == L'-') continue;  // other flags: accept
    if (!DeleteFileW(arg.c_str())) {
      if (!force) {
        diag_line(L"rm: cannot remove file");
        rc = 1;
      }
    }
  }
  return rc;
}

int demo_dispatch(const std::wstring &applet,
                  const std::vector<std::wstring> &args) {
  if (applet == L"cat") return demo_cat(args);
  if (applet == L"cp") return demo_cp(args);
  if (applet == L"rm") return demo_rm(args);
  return -1;
}
