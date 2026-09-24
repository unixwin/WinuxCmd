#include "framework/winuxtest.h"

namespace {

class ScopedProcess {
 public:
  ScopedProcess() = default;
  explicit ScopedProcess(PROCESS_INFORMATION process_info)
      : process_info_(process_info), active_(true) {}

  ScopedProcess(const ScopedProcess&) = delete;
  ScopedProcess& operator=(const ScopedProcess&) = delete;

  ScopedProcess(ScopedProcess&& other) noexcept
      : process_info_(other.process_info_), active_(other.active_) {
    other.active_ = false;
  }

  ScopedProcess& operator=(ScopedProcess&& other) noexcept {
    if (this != &other) {
      cleanup();
      process_info_ = other.process_info_;
      active_ = other.active_;
      other.active_ = false;
    }
    return *this;
  }

  ~ScopedProcess() { cleanup(); }

  [[nodiscard]] bool valid() const { return active_; }
  [[nodiscard]] DWORD pid() const { return process_info_.dwProcessId; }

 private:
  void cleanup() {
    if (!active_) return;
    if (WaitForSingleObject(process_info_.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(process_info_.hProcess, 99);
      WaitForSingleObject(process_info_.hProcess, 5000);
    }
    CloseHandle(process_info_.hProcess);
    CloseHandle(process_info_.hThread);
    active_ = false;
  }

  PROCESS_INFORMATION process_info_{};
  bool active_ = false;
};

ScopedProcess start_token_child(const std::wstring& token) {
  wchar_t system_dir[MAX_PATH]{};
  UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
  EXPECT_GT(len, 0U);
  if (len == 0 || len >= MAX_PATH) return {};

  std::wstring powershell =
      std::wstring(system_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
  std::wstring cmd_line =
      L"\"" + powershell +
      L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass "
      L"-Command \"$x='" +
      token + L"'; Start-Sleep -Seconds 30\"";

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process_info{};
  BOOL ok = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                           &process_info);
  EXPECT_TRUE(ok != FALSE);
  if (!ok) return {};

  Sleep(500);
  return ScopedProcess(process_info);
}

}  // namespace

TEST(pgrep, pgrep_full_command_line_finds_process) {
  auto child = start_token_child(L"WINUXCMD_PGREP_TOKEN_ALPHA");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pgrep.exe", {L"-f", L"WINUXCMD_PGREP_TOKEN_ALPHA"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
}

TEST(pgrep, pgrep_list_full_prints_command_line) {
  auto child = start_token_child(L"WINUXCMD_PGREP_TOKEN_BETA");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pgrep.exe", {L"-a", L"-f", L"WINUXCMD_PGREP_TOKEN_BETA"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("WINUXCMD_PGREP_TOKEN_BETA") !=
              std::string::npos);
}

TEST(pgrep, pgrep_count_no_match_returns_one) {
  Pipeline p;
  p.add(L"pgrep.exe", {L"-c", L"WINUXCMD_PROCESS_THAT_SHOULD_NOT_EXIST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "0\n");
}
