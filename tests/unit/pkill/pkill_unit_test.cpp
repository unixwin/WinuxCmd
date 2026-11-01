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

  [[nodiscard]] bool is_running() const {
    if (!active_) return false;
    return WaitForSingleObject(process_info_.hProcess, 0) == WAIT_TIMEOUT;
  }

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

TEST(pkill, pkill_zero_checks_match_without_terminating) {
  auto child = start_token_child(L"WINUXCMD_PKILL_CHECK_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pkill.exe", {L"-0", L"-f", L"WINUXCMD_PKILL_CHECK_TOKEN"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(child.is_running());
}

TEST(pkill, pkill_full_command_line_terminates_match) {
  auto child = start_token_child(L"WINUXCMD_PKILL_TERM_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pkill.exe", {L"-f", L"WINUXCMD_PKILL_TERM_TOKEN"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(child.is_running());
}

TEST(pkill, pkill_no_match_returns_one) {
  Pipeline p;
  p.add(L"pkill.exe", {L"-f", L"WINUXCMD_PKILL_NO_SUCH_PROCESS"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

// -e echoes "<name> killed (pid <pid>)" for each process signaled
// (procps pgrep.c output_strlist echo format).
TEST(pkill, pkill_echo_reports_killed_pid) {
  auto child = start_token_child(L"WINUXCMD_PKILL_ECHO_TOKEN");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"pkill.exe", {L"-e", L"-f", L"WINUXCMD_PKILL_ECHO_TOKEN"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("killed (pid " + std::to_string(child.pid()) +
                                 ")") != std::string::npos);
  EXPECT_FALSE(child.is_running());
}

// -c prints the number of matching processes; -n reduces the
// selection to the newest one.
TEST(pkill, pkill_count_and_newest) {
  auto child_a = start_token_child(L"WINUXCMD_PKILL_CNT_A");
  auto child_b = start_token_child(L"WINUXCMD_PKILL_CNT_B");
  EXPECT_TRUE(child_a.valid());
  EXPECT_TRUE(child_b.valid());
  if (!child_a.valid() || !child_b.valid()) return;

  {
    Pipeline p;
    p.add(L"pkill.exe", {L"-0", L"-c", L"-f", L"WINUXCMD_PKILL_CNT"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "2\n");
  }
  {
    Pipeline p;
    p.add(L"pkill.exe", {L"-0", L"-c", L"-n", L"-f", L"WINUXCMD_PKILL_CNT"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_text, "1\n");
  }
  EXPECT_TRUE(child_a.is_running());
  EXPECT_TRUE(child_b.is_running());
}

// A signal may be spelled as a bare option (-TERM) or via --signal.
TEST(pkill, pkill_signal_spellings) {
  auto child_a = start_token_child(L"WINUXCMD_PKILL_SIG_A");
  EXPECT_TRUE(child_a.valid());
  if (!child_a.valid()) return;

  {
    Pipeline p;
    p.add(L"pkill.exe", {L"-TERM", L"-f", L"WINUXCMD_PKILL_SIG_A"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(child_a.is_running());
  }

  auto child_b = start_token_child(L"WINUXCMD_PKILL_SIG_B");
  EXPECT_TRUE(child_b.valid());
  if (!child_b.valid()) return;

  {
    Pipeline p;
    p.add(L"pkill.exe", {L"--signal", L"KILL", L"-f", L"WINUXCMD_PKILL_SIG_B"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(child_b.is_running());
  }
}
