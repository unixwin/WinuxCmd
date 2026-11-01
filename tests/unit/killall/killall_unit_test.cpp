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

// -l lists the signal names and exits 0 without needing a name.
TEST(killall, killall_list_signals) {
  Pipeline p;
  p.add(L"killall.exe", {L"-l"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
}

// A name with no matching process reports "<name>: no process found"
// (psmisc) and exits 1.
TEST(killall, killall_no_process_found_message) {
  Pipeline p;
  p.add(L"killall.exe", {L"winuxcmd-no-such-process-xyz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(
      r.stderr_text.find("winuxcmd-no-such-process-xyz: no process found") !=
      std::string::npos);
}

// -r treats the name as a regular expression.
TEST(killall, killall_regexp_terminates_match) {
  auto child = start_unique_child(L"winuxcmd_killall_regexp_unique");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"killall.exe", {L"-r", L"winuxcmd_killall_regexp_.*"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(child.is_running());
}

// -v reports each signaled process.
TEST(killall, killall_verbose_reports_pid) {
  auto child = start_unique_child(L"winuxcmd_killall_verbose_unique");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"killall.exe", {L"-v", L"-e", L"winuxcmd_killall_verbose_unique"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::to_string(child.pid())) !=
              std::string::npos);
  EXPECT_FALSE(child.is_running());
}

// -y/-o filter by process age: a just-started child is younger than
// an hour but not older than an hour.
TEST(killall, killall_age_filters) {
  auto child = start_unique_child(L"winuxcmd_killall_age_unique");
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  {
    Pipeline p;
    p.add(L"killall.exe",
          {L"-0", L"-y", L"1h", L"-e", L"winuxcmd_killall_age_unique"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(child.is_running());
  }
  {
    Pipeline p;
    p.add(L"killall.exe",
          {L"-0", L"-o", L"1h", L"-e", L"winuxcmd_killall_age_unique"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_TRUE(child.is_running());
  }
}
