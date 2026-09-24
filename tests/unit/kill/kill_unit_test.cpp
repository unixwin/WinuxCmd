// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

  bool wait_for_exit(DWORD timeout_ms) {
    if (!active_) return true;
    return WaitForSingleObject(process_info_.hProcess, timeout_ms) ==
           WAIT_OBJECT_0;
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

TEST(kill, list_signals) {
  Pipeline p;
  p.add(L"kill.exe", {L"-l"});

  TEST_LOG_CMD_LIST("kill.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -l output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("INT") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KILL") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("USR1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("RTMIN+<N>") != std::string::npos);
}

TEST(kill, list_signals_long) {
  Pipeline p;
  p.add(L"kill.exe", {L"--list"});

  TEST_LOG_CMD_LIST("kill.exe", L"--list");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe --list output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("USR2") != std::string::npos);
}

TEST(kill, list_signals_table) {
  Pipeline p;
  p.add(L"kill.exe", {L"-L"});

  TEST_LOG_CMD_LIST("kill.exe", L"-L");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -L output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(" 1 HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("30 USR1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("32 RTMIN") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("64 RTMAX") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KILL") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
}

TEST(kill, list_signals_table_long) {
  Pipeline p;
  p.add(L"kill.exe", {L"--table"});

  TEST_LOG_CMD_LIST("kill.exe", L"--table");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe --table output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(" 1 HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("32 RTMIN") != std::string::npos);
}

TEST(kill, convert_signal_name_to_number_inline) {
  Pipeline p;
  p.add(L"kill.exe", {L"-lHUP"});

  TEST_LOG_CMD_LIST("kill.exe", L"-lHUP");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -lHUP output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n");
}

TEST(kill, convert_sig_prefixed_name_to_number_with_space) {
  Pipeline p;
  p.add(L"kill.exe", {L"-l", L"SIGHUP"});

  TEST_LOG_CMD_LIST("kill.exe", L"-l", L"SIGHUP");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -l SIGHUP output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n");
}

TEST(kill, convert_signal_number_to_name_inline) {
  Pipeline p;
  p.add(L"kill.exe", {L"-l1"});

  TEST_LOG_CMD_LIST("kill.exe", L"-l1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -l1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "HUP\n");
}

TEST(kill, convert_realtime_signal_number_to_name) {
  Pipeline p;
  p.add(L"kill.exe", {L"-l", L"34"});

  TEST_LOG_CMD_LIST("kill.exe", L"-l", L"34");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -l 34 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "RT2\n");
}

TEST(kill, convert_realtime_signal_name_to_number) {
  Pipeline p;
  p.add(L"kill.exe", {L"-lRTMIN+1"});

  TEST_LOG_CMD_LIST("kill.exe", L"-lRTMIN+1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -lRTMIN+1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "33\n");
}

TEST(kill, invalid_pid) {
  Pipeline p;
  p.add(L"kill.exe", {L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe 99999999 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, invalid_signal_name) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"INVALID", L"1234"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"INVALID", L"1234");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s INVALID stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("unknown signal") != std::string::npos ||
              r.stderr_text.find("invalid signal") != std::string::npos);
}

TEST(kill, invalid_signal_number) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"999", L"1234"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"999", L"1234");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s 999 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid signal") != std::string::npos);
}

TEST(kill, no_pid_specified) {
  Pipeline p;
  p.add(L"kill.exe", {});

  TEST_LOG_CMD_LIST("kill.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe (no args) stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("no process ID") != std::string::npos ||
              r.stderr_text.find("PID") != std::string::npos);
}

TEST(kill, invalid_pid_format) {
  Pipeline p;
  p.add(L"kill.exe", {L"abc"});

  TEST_LOG_CMD_LIST("kill.exe", L"abc");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe abc stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid PID") != std::string::npos);
}

TEST(kill, kill_with_sigterm) {
  Pipeline p;
  p.add(L"kill.exe", {L"-15", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-15", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -15 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, kill_with_sigkill) {
  Pipeline p;
  p.add(L"kill.exe", {L"-9", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-9", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -9 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, multiple_pids) {
  Pipeline p;
  p.add(L"kill.exe", {L"99999998", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"99999998", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe multiple PIDs stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("99999998") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("99999999") != std::string::npos);
}

TEST(kill, signal_by_name) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"TERM", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"TERM", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s TERM stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, signal_with_sig_prefix) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"SIGKILL", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"SIGKILL", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s SIGKILL stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, signal_zero_current_process) {
  Pipeline p;
  p.add(L"kill.exe", {L"-0", std::to_wstring(GetCurrentProcessId())});

  TEST_LOG_CMD_LIST("kill.exe", L"-0", L"<current-pid>");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -0 current pid stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(kill, queue_option_reports_windows_difference) {
  Pipeline p;
  p.add(L"kill.exe",
        {L"-q", L"42", L"-0", std::to_wstring(GetCurrentProcessId())});

  TEST_LOG_CMD_LIST("kill.exe", L"-q", L"42", L"-0", L"<current-pid>");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -q 42 -0 current pid stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find(
                  "queueing signal payloads is not supported on Windows") !=
              std::string::npos);
}

TEST(kill, queue_option_rejects_non_integer) {
  Pipeline p;
  p.add(L"kill.exe",
        {L"-q", L"abc", L"-0", std::to_wstring(GetCurrentProcessId())});

  TEST_LOG_CMD_LIST("kill.exe", L"-q", L"abc", L"-0", L"<current-pid>");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -q abc stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("queue value must be an integer") !=
              std::string::npos);
}

TEST(kill, signal_name_obsolete_form_usr1_is_accepted) {
  Pipeline p;
  p.add(L"kill.exe", {L"-USR1", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-USR1", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -USR1 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, terminates_controlled_child_with_sigkill) {
  auto child = start_sleep_child();
  EXPECT_TRUE(child.valid());
  if (!child.valid()) return;

  Pipeline p;
  p.add(L"kill.exe", {L"-9", std::to_wstring(child.pid())});

  TEST_LOG_CMD_LIST("kill.exe", L"-9", L"<child-pid>");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -9 child stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(child.wait_for_exit(5000));
}
