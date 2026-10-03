// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(yes, yes_default) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"5");
  p.add(L"yes.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "y\ny\ny\ny\ny\n");
}

TEST(yes, yes_custom_string) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"hello"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\nhello\nhello\n");
}

TEST(yes, yes_joins_all_arguments_with_spaces) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"a", L"bar", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a bar c\na bar c\na bar c\n");
}

TEST(yes, yes_version_succeeds) {
  Pipeline p;
  p.add(L"yes.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("yes (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(yes, yes_double_dash_keeps_version_literal) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"--", L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "--version\n--version\n--version\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

// ---------------------------------------------------------------------------
// Issue #1142: GNU yes dies of SIGPIPE once the pipe reader exits, so a
// shell reports 128 + 13 = 141 for the yes side of `yes | head -3`. Windows
// has no SIGPIPE, so yes.exe must emulate the signal death with exit
// status 141 when its stdout write fails because the pipe is gone. The
// native-parent probe below mirrors the issue's python repro: yes runs on
// a pipe whose read end is closed before it writes, and the raw process
// exit code is read without any shell signal decoding.
// ---------------------------------------------------------------------------
TEST(yes, yes_broken_pipe_exits_141_like_sigpipe_death) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  HANDLE out_r = nullptr;
  HANDLE out_w = nullptr;
  CreatePipe(&out_r, &out_w, &sa, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = out_w;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  auto yes_exe = ProjectPaths::exe(L"yes.exe").wstring();
  std::wstring command = L"\"" + yes_exe + L"\"";

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                      &si, &pi)) {
    CloseHandle(out_r);
    CloseHandle(out_w);
    throw std::runtime_error("CreateProcessW(yes.exe) failed: " +
                             std::to_string(GetLastError()));
  }

  // Drop both ends of the parent's pipe view: the reader is gone before
  // yes writes, so its stdout is a broken pipe from the first flush.
  CloseHandle(out_w);
  CloseHandle(out_r);
  ResumeThread(pi.hThread);

  ASSERT_NE(WaitForSingleObject(pi.hProcess, 30000), WAIT_FAILED);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  EXPECT_EQ(static_cast<int>(exit_code), 141);
}

// A live consumer drains the pipe and exits first; the producer must still
// classify the subsequent write failure as SIGPIPE death (141), never as a
// fatal write error (1) and never hang until a coordinator kills it.
TEST(yes, yes_pipe_to_exited_reader_exits_141) {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  HANDLE out_r = nullptr;
  HANDLE out_w = nullptr;
  CreatePipe(&out_r, &out_w, &sa, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = out_w;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  auto yes_exe = ProjectPaths::exe(L"yes.exe").wstring();
  std::wstring command = L"\"" + yes_exe + L"\"";

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(out_r);
    CloseHandle(out_w);
    throw std::runtime_error("CreateProcessW(yes.exe) failed: " +
                             std::to_string(GetLastError()));
  }
  CloseHandle(out_w);

  // Consume one chunk like a real `head` would, then leave: closing the
  // read end mid-stream is what makes the producer's next write fail.
  char buffer[4096];
  DWORD read = 0;
  ASSERT_TRUE(ReadFile(out_r, buffer, sizeof(buffer), &read, nullptr));
  EXPECT_GT(read, 0u);
  CloseHandle(out_r);

  ASSERT_NE(WaitForSingleObject(pi.hProcess, 30000), WAIT_FAILED);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  EXPECT_EQ(static_cast<int>(exit_code), 141);
}
