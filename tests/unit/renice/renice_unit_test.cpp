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

ScopedProcess start_sleep_child() {
  wchar_t system_dir[MAX_PATH]{};
  UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
  EXPECT_GT(len, 0U);
  if (len == 0 || len >= MAX_PATH) return {};

  std::wstring powershell =
      std::wstring(system_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
  std::wstring cmd_line =
      L"\"" + powershell +
      L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass "
      L"-Command \"Start-Sleep -Seconds 30\"";

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process_info{};
  BOOL ok = CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                           &process_info);
  EXPECT_TRUE(ok != FALSE);
  if (!ok) return {};

  Sleep(300);
  return ScopedProcess(process_info);
}

}  // namespace

TEST(renice, renice_sets_spawned_process_priority) {
  auto child = start_sleep_child();
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"renice.exe", {L"10", L"-p", std::to_wstring(child.pid())});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new priority 10") != std::string::npos);
}

TEST(renice, renice_rejects_invalid_pid) {
  Pipeline p;
  p.add(L"renice.exe", {L"10", L"not-a-pid"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid process ID") != std::string::npos);
}

TEST(renice, renice_requires_pid) {
  Pipeline p;
  p.add(L"renice.exe", {L"10"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("missing process ID") != std::string::npos);
}
