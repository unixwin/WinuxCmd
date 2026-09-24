#include "framework/winuxtest.h"

namespace {

class ScopedProcess {
 public:
  ScopedProcess() = default;
  explicit ScopedProcess(PROCESS_INFORMATION process_info)
      : process_info_(process_info), active_(true) {}

  ScopedProcess(const ScopedProcess&) = delete;
  ScopedProcess& operator=(const ScopedProcess&) = delete;

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

ScopedProcess start_sleep_child(const std::wstring& token) {
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

TEST(pidof, pidof_finds_process_by_name) {
  auto child = start_sleep_child(L"WINUXCMD_PIDOF_NAME_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pidof.exe", {L"powershell"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
}

TEST(pidof, pidof_script_token_is_not_visible_on_windows) {
  auto child = start_sleep_child(L"WINUXCMD_PIDOF_SCRIPT_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pidof.exe", {L"WINUXCMD_PIDOF_SCRIPT_TOKEN"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pidof, pidof_exact_matches_process_name) {
  auto child = start_sleep_child(L"WINUXCMD_PIDOF_EXACT_NAME_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pidof.exe", {L"--exact", L"powershell"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pidof, pidof_exact_does_not_match_command_line_token) {
  auto child = start_sleep_child(L"WINUXCMD_PIDOF_EXACT_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pidof.exe", {L"--exact", L"WINUXCMD_PIDOF_EXACT_TOKEN"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pidof, pidof_no_match_returns_one) {
  Pipeline p;
  p.add(L"pidof.exe", {L"WINUXCMD_PROCESS_THAT_SHOULD_NOT_EXIST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}
