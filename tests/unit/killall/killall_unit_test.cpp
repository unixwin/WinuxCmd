#include "framework/winuxtest.h"

namespace {

class ScopedProcess {
 public:
  ScopedProcess() = default;
  ScopedProcess(PROCESS_INFORMATION process_info, std::wstring image_path)
      : process_info_(process_info),
        image_path_(std::move(image_path)),
        active_(true) {}

  ScopedProcess(const ScopedProcess&) = delete;
  ScopedProcess& operator=(const ScopedProcess&) = delete;

  ~ScopedProcess() { cleanup(); }

  [[nodiscard]] bool valid() const { return active_; }
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
    if (!image_path_.empty()) DeleteFileW(image_path_.c_str());
    active_ = false;
  }

  PROCESS_INFORMATION process_info_{};
  std::wstring image_path_;
  bool active_ = false;
};

ScopedProcess start_unique_child(const std::wstring& image_name) {
  wchar_t system_dir[MAX_PATH]{};
  UINT len = GetSystemDirectoryW(system_dir, MAX_PATH);
  EXPECT_GT(len, 0U);
  if (len == 0 || len >= MAX_PATH) return {};

  wchar_t temp_dir[MAX_PATH]{};
  DWORD temp_len = GetTempPathW(MAX_PATH, temp_dir);
  EXPECT_GT(temp_len, 0U);
  if (temp_len == 0 || temp_len >= MAX_PATH) return {};

  std::wstring source =
      std::wstring(system_dir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
  std::wstring target = std::wstring(temp_dir) + image_name + L".exe";
  DeleteFileW(target.c_str());
  EXPECT_TRUE(CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE);
  if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) return {};

  std::wstring cmd_line =
      L"\"" + target +
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

  Sleep(500);
  return ScopedProcess(process_info, target);
}

}  // namespace

TEST(killall, killall_zero_checks_name_without_terminating) {
  auto child = start_unique_child(L"winuxcmd_killall_check_unique");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"killall.exe", {L"-0", L"-e", L"winuxcmd_killall_check_unique"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(child.is_running());
}

TEST(killall, killall_exact_name_terminates_match) {
  auto child = start_unique_child(L"winuxcmd_killall_term_unique");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"killall.exe", {L"-e", L"winuxcmd_killall_term_unique"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(child.is_running());
}

TEST(killall, killall_quiet_no_match_returns_one) {
  Pipeline p;
  p.add(L"killall.exe", {L"-q", L"WINUXCMD_KILLALL_NO_SUCH_PROCESS"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.empty());
}
