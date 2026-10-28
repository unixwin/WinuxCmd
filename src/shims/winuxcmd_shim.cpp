// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//
/// @Description: Package shim - run the payload of a `layout: shim` package.
/// @License: MIT
//
// `usr\bin\<command>.exe` for a shim-layout package used to be a hardlink to an
// older winuxcmd.exe whose only job was to spawn `opt\<pkg>\<command>.exe` and
// wait. That cost a whole WinuxCmd startup on every invocation - measured at
// 22-26 ms per call for python/node/curl/openssl/ssh - while the payload itself
// starts in a fraction of that (unixwin/WinuxCmd#1207).
//
// This is the same forwarding idea without the dispatcher: read the payload
// path from `<root>\.wpm\shims.map`, start it, forward stdio and the exit code.
// The layout stays isolated (private DLLs keep living beside the payload, never
// in a directory on %PATH%), which is the reason the shim exists at all.
//
// Arguments are spliced **verbatim** out of GetCommandLineW(): nothing is
// re-quoted. Re-quoting is how a forwarder corrupts arguments, and the rule to
// follow is the one the shell already follows - pass what you were given.

#include <windows.h>

#pragma comment(lib, "user32.lib")

#include <cstdio>
#include <string>
#include <vector>

#include "../shared/argv_adapters.h"

namespace {

// The payload map, relative to the installation root.
constexpr wchar_t kShimMap[] = L".wpm\\shims.map";
// A command that is not registered is as good as missing.
constexpr int kNotFound = 127;
// A registered command that cannot be started.
constexpr int kCannotStart = 126;

std::wstring executable_path() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return {};
    if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring_view file_name_of(std::wstring_view path) {
  const auto separator = path.find_last_of(L"\\/");
  return separator == std::wstring_view::npos ? path : path.substr(separator + 1);
}

std::wstring_view parent_of(std::wstring_view path) {
  const auto separator = path.find_last_of(L"\\/");
  return separator == std::wstring_view::npos ? std::wstring_view{}
                                             : path.substr(0, separator);
}

std::wstring lower(std::wstring_view text) {
  std::wstring lowered(text);
  for (auto& ch : lowered) ch = static_cast<wchar_t>(towlower(ch));
  return lowered;
}

std::wstring trim(std::wstring_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == L' ' || text[begin] == L'\t')) ++begin;
  while (end > begin &&
         (text[end - 1] == L'\r' || text[end - 1] == L'\n' || text[end - 1] == L' ' ||
          text[end - 1] == L'\t'))
    --end;
  return std::wstring(text.substr(begin, end - begin));
}

/// Look up `command` in the shim map. Each line is `command=payload`, payload
/// relative to the installation root; `#` starts a comment.
std::wstring payload_for(const std::wstring& root, const std::wstring& command) {
  const std::wstring path = root + L"\\" + kShimMap;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr) return {};
  std::string contents;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) contents.append(chunk, read);
  fclose(file);

  // The map is written by wpm as UTF-8; command names and paths here are ASCII.
  const std::wstring wanted = lower(command);
  size_t position = 0;
  while (position <= contents.size()) {
    const size_t end = contents.find('\n', position);
    const std::string line =
        contents.substr(position, end == std::string::npos ? std::string::npos
                                                           : end - position);
    position = end == std::string::npos ? contents.size() + 1 : end + 1;
    const auto equals = line.find('=');
    if (equals == std::string::npos || line.empty() || line[0] == '#') continue;
    const std::wstring key = lower(trim(std::wstring(line.begin(), line.begin() + static_cast<long>(equals))));
    if (key != wanted) continue;
    const std::string value = line.substr(equals + 1);
    return trim(std::wstring(value.begin(), value.end()));
  }
  return {};
}

/// Everything after the program token of `command_line`, verbatim. Windows
/// splits the command line itself, so this only has to find where the program
/// token ends - no quoting is interpreted or produced here.
std::wstring_view arguments_after_program(std::wstring_view command_line) {
  size_t index = 0;
  while (index < command_line.size() &&
         (command_line[index] == L' ' || command_line[index] == L'\t'))
    ++index;
  if (index < command_line.size() && command_line[index] == L'"') {
    ++index;
    while (index < command_line.size()) {
      if (command_line[index] == L'\\') {
        index += 2;
        continue;
      }
      if (command_line[index] == L'"') {
        ++index;
        break;
      }
      ++index;
    }
  } else {
    while (index < command_line.size() && command_line[index] != L' ' &&
           command_line[index] != L'\t')
      ++index;
  }
  return command_line.substr(index);
}

bool any_std_stream_is_console() {
  const DWORD std_streams[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  for (const DWORD stream : std_streams) {
    const HANDLE handle = GetStdHandle(stream);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) continue;
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) return true;
  }
  return false;
}

/// Free a visible console that Windows auto-allocated for this process alone
/// when all stdio streams are redirected. An inherited terminal, a ConPTY
/// pseudoconsole (invisible window), or a hidden CREATE_NO_WINDOW console is
/// kept alive so the payload and its grandchildren (`python` -> `git`) inherit
/// it for free without flashing visible console windows.
void release_orphan_console() {
  if (any_std_stream_is_console()) return;
  if (GetConsoleCP() == 0) return;
  const HWND window = GetConsoleWindow();
  if (window == nullptr || IsWindowVisible(window) == 0) return;
  DWORD pids[64]{};
  if (GetConsoleProcessList(pids, 64) != 1) return;
  FreeConsole();
}

/// When this shim already has a console attached (inherited terminal, ConPTY,
/// or a hidden console from `niu.exe`), pass 0 so the payload inherits it even
/// when stdio is piped. Only when the shim itself is console-less
/// (`GetConsoleCP() == 0`) do we allocate ONE hidden console via
/// `CREATE_NO_WINDOW` — never `DETACHED_PROCESS`, which leaves the payload
/// console-less and makes every console-subsystem grandchild (`git.exe`, etc.)
/// pop up a visible console window (niubash#451/#462).
DWORD child_creation_flags() {
  return GetConsoleCP() == 0 ? CREATE_NO_WINDOW : 0;
}

// The argv-adapter table (payloads that need a fixed argv prefix, e.g.
// gawk's `-v BINMODE=3`; unixwin/niubash#467) lives in
// src/shared/argv_adapters.h, shared with the multiplexer's forward path -
// see that header for the full rationale.

}  // namespace

int wmain(int /*argc*/, wchar_t** /*argv*/) {
  release_orphan_console();
  const std::wstring self = executable_path();
  const std::wstring_view name = file_name_of(self);
  std::wstring command(name.substr(0, name.size() > 4 ? name.size() - 4 : name.size()));
  const std::wstring_view bin_dir = parent_of(self);
  // The canonical placement is <root>\usr\bin\<cmd>.exe, two directories below
  // the installation root. Commands forwarded through an argv adapter get
  // their PATH-entry link (<root>\bin\<cmd>.exe) pointed at this shim as well,
  // which sits one level closer to the root - so probe the canonical depth
  // first, then the shallower one, for the map itself. A missing map at both
  // depths keeps the old "no payload registered" outcome below.
  const std::wstring_view candidates[] = {parent_of(parent_of(bin_dir)),
                                          parent_of(bin_dir)};
  std::wstring_view root = candidates[0];
  for (const std::wstring_view candidate : candidates) {
    if (candidate.empty()) continue;
    const std::wstring map = std::wstring(candidate) + L"\\" + kShimMap;
    if (GetFileAttributesW(map.c_str()) != INVALID_FILE_ATTRIBUTES) {
      root = candidate;
      break;
    }
  }
  if (root.empty()) {
    fwprintf(stderr, L"winuxcmd-shim: cannot locate the installation root from '%ls'\n",
             self.c_str());
    return kCannotStart;
  }

  const std::wstring registered = payload_for(std::wstring(root), command);
  if (registered.empty()) {
    fwprintf(stderr,
             L"winuxcmd-shim: no payload is registered for '%ls'; reinstall the "
             L"package that provides it ('wpm install <package> --force')\n",
             command.c_str());
    return kNotFound;
  }

  const std::wstring payload = std::wstring(root) + L"\\" + registered;
  if (GetFileAttributesW(payload.c_str()) == INVALID_FILE_ATTRIBUTES) {
    fwprintf(stderr, L"winuxcmd-shim: the payload for '%ls' is missing: %ls\n",
             command.c_str(), payload.c_str());
    return kNotFound;
  }

  std::wstring command_line = L"\"" + payload + L"\"" +
                              std::wstring(argv_adapters::prefix_for(command)) +
                              std::wstring(arguments_after_program(GetCommandLineW()));

  // Hand the child *our* standard handles explicitly. A detached child does not
  // inherit a console and gets no standard handles by default, so without this
  // its stdout/stderr go nowhere and its exit code arrives alone - measured as
  // silent output from an otherwise working forward.
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(payload.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                      child_creation_flags(), nullptr, nullptr, &startup, &child)) {
    fwprintf(stderr, L"winuxcmd-shim: cannot start %ls: error %lu\n", payload.c_str(),
             GetLastError());
    return kCannotStart;
  }
  CloseHandle(child.hThread);
  WaitForSingleObject(child.hProcess, INFINITE);
  DWORD code = kCannotStart;
  GetExitCodeProcess(child.hProcess, &code);
  CloseHandle(child.hProcess);
  return static_cast<int>(code);
}
