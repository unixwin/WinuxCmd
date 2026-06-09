/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software", to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: tty_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {

auto run_tty_with_closed_stdout() -> CommandResult {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  HANDLE in_r = nullptr;
  HANDLE in_w = nullptr;
  HANDLE out_r = nullptr;
  HANDLE out_w = nullptr;
  HANDLE err_r = nullptr;
  HANDLE err_w = nullptr;

  CreatePipe(&in_r, &in_w, &sa, 0);
  CreatePipe(&out_r, &out_w, &sa, 0);
  CreatePipe(&err_r, &err_w, &sa, 0);

  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = err_w;

  auto tty_exe = ProjectPaths::exe(L"tty.exe").wstring();
  std::wstring command = L"\"" + tty_exe + L"\"";

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                      &si, &pi)) {
    DWORD err = GetLastError();
    CloseHandle(in_r);
    CloseHandle(in_w);
    CloseHandle(out_r);
    CloseHandle(out_w);
    CloseHandle(err_r);
    CloseHandle(err_w);
    throw std::runtime_error("CreateProcessW failed: " + std::to_string(err));
  }

  CloseHandle(in_r);
  CloseHandle(out_w);
  CloseHandle(err_w);
  CloseHandle(in_w);
  CloseHandle(out_r);
  ResumeThread(pi.hThread);

  std::string stderr_text;
  char buffer[256];
  DWORD read = 0;
  while (ReadFile(err_r, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    stderr_text.append(buffer, buffer + read);
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(err_r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return {static_cast<int>(exit_code), "", stderr_text};
}

}  // namespace

TEST(tty, tty_basic) {
  Pipeline p;
  p.add(L"tty.exe", {});

  TEST_LOG_CMD_LIST("tty.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty output", r.stdout_text);

  // In test environment, not a TTY
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "not a tty\n");
}

TEST(tty, tty_silent) {
  Pipeline p;
  p.add(L"tty.exe", {L"-s"});

  TEST_LOG_CMD_LIST("tty.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty output", r.stdout_text);

  // In test environment, not a TTY - silent mode
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(tty, tty_quiet_alias) {
  Pipeline p;
  p.add(L"tty.exe", {L"--quiet"});

  TEST_LOG_CMD_LIST("tty.exe", L"--quiet");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(tty, tty_rejects_operand) {
  Pipeline p;
  p.add(L"tty.exe", {L"extra"});

  TEST_LOG_CMD_LIST("tty.exe", L"extra");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tty: extra operand 'extra'\nTry 'tty --help' for more "
                 "information.\n");
}

TEST(tty, tty_invalid_option_returns_2_with_help_hint) {
  Pipeline p;
  p.add(L"tty.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "tty: unrecognized option '--invalid'\n"
                 "Try 'tty --help' for more information.\n");
}

TEST(tty, tty_returns_three_when_stdout_pipe_is_closed) {
  auto r = run_tty_with_closed_stdout();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tty stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 3);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}
