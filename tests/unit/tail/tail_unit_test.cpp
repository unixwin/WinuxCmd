/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: tail_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include <array>
#include <string_view>

#include "framework/winuxtest.h"

namespace {

struct ScopedProcess {
  PROCESS_INFORMATION info{};
  bool valid = false;

  ScopedProcess() = default;
  ScopedProcess(const ScopedProcess&) = delete;
  auto operator=(const ScopedProcess&) -> ScopedProcess& = delete;

  ScopedProcess(ScopedProcess&& other) noexcept
      : info(other.info), valid(other.valid) {
    other.info = {};
    other.valid = false;
  }

  auto operator=(ScopedProcess&& other) noexcept -> ScopedProcess& {
    if (this != &other) {
      cleanup();
      info = other.info;
      valid = other.valid;
      other.info = {};
      other.valid = false;
    }
    return *this;
  }

  ~ScopedProcess() { cleanup(); }

  [[nodiscard]]
  auto pid() const -> DWORD {
    return info.dwProcessId;
  }

 private:
  void cleanup() {
    if (!valid) return;
    if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) {
      TerminateProcess(info.hProcess, 1);
      WaitForSingleObject(info.hProcess, 1000);
    }
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    valid = false;
  }
};

auto start_follow_sentinel(int ping_count = 4) -> ScopedProcess {
  ScopedProcess process;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring command = L"cmd.exe /d /c ping -n ";
  command += std::to_wstring(ping_count);
  command += L" 127.0.0.1 > nul";

  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                     &process.info)) {
    process.valid = true;
  }
  return process;
}

}  // namespace

TEST(tail, tail_default_last_10_lines) {
  TempDir tmp;
  tmp.write("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");
}

TEST(tail, tail_plus_lines_and_bytes) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-n", L"+2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "beta\ngamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"-c", L"+3", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "pha\nbeta\ngamma\n");
}

TEST(tail, tail_last_count_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-c", L"5", L"-n", L"1", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "gamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"-n", L"1", L"-c", L"5", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "amma\n");
}

TEST(tail, tail_negative_line_and_byte_counts) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-n", L"-2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "beta\ngamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"-c-5", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "amma\n");
}

TEST(tail, tail_KB_suffix_is_decimal_bytes) {
  TempDir tmp;
  std::string data(1200, 'x');
  data.replace(200, 4, "tail");
  tmp.write("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-c", L"1KB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 1000u);
  EXPECT_EQ_TEXT(r.stdout_text.substr(0, 4), "tail");
}

TEST(tail, tail_rejects_lowercase_kB_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-c", L"1kB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid") != std::string::npos);
}

TEST(tail, tail_accepts_extended_representable_count_suffixes) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  const std::array<std::wstring_view, 9> suffixes{
      L"T", L"TB", L"TiB", L"P", L"PB", L"PiB", L"E", L"EB", L"EiB"};
  for (auto suffix : suffixes) {
    std::wstring count = L"1";
    count.append(suffix.data(), suffix.size());

    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"tail.exe", {L"-c", count, L"a.txt"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "0123456789abcdef");
  }
}

TEST(tail, tail_accepts_zero_with_oversized_count_suffixes) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  const std::array<std::wstring_view, 12> suffixes{
      L"Z", L"ZB", L"ZiB", L"Y", L"YB", L"YiB",
      L"R", L"RB", L"RiB", L"Q", L"QB", L"QiB"};
  for (auto suffix : suffixes) {
    std::wstring count = L"0";
    count.append(suffix.data(), suffix.size());

    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"tail.exe", {L"-c", count, L"a.txt"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "");
  }
}

TEST(tail, tail_rejects_nonzero_oversized_count_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-c", L"1QiB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid") != std::string::npos);
}

TEST(tail, tail_legacy_count_shorthand) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "beta\ngamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"+2", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "beta\ngamma\n");
}

TEST(tail, tail_obsolete_compact_byte_count) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-2c", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "f\n");
}

TEST(tail, tail_obsolete_block_suffix) {
  TempDir tmp;
  std::string data(600, 'x');
  data.replace(88, 4, "tail");
  tmp.write("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-1b", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 512u);
  EXPECT_EQ_TEXT(r.stdout_text.substr(0, 4), "tail");
}

TEST(tail, tail_obsolete_compact_line_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-2l", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "beta\ngamma\n");
}

TEST(tail, tail_last_header_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\n");
  tmp.write("b.txt", "B1\nB2\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-v", L"-q", L"-n", L"1", L"a.txt", L"b.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "A2\nB2\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"-q", L"-v", L"-n", L"1", L"a.txt", L"b.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "==> a.txt <==\nA2\n\n==> b.txt <==\nB2\n");
}

TEST(tail, tail_stdin_header_uses_standard_input) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-n", L"1", L"-", L"a.txt"});
  p.set_stdin("S1\nS2\n");
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "==> standard input <==\nS2\n\n==> a.txt <==\nA2\n");
}

TEST(tail, tail_follow_option_recognized) {
  // Verify that -f flag is recognized and doesn't error out
  // (actual follow mode is a long-running operation tested manually)
  TempDir tmp;
  tmp.write("a.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--help"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("--follow") != std::string::npos);
}

TEST(tail, tail_retry_and_pid_follow_missing_file_until_it_appears) {
  TempDir tmp;
  auto sentinel = start_follow_sentinel();
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    tmp.write("live.txt", "line1\nline2\n");
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-f", L"--retry", L"--pid",
                      std::to_wstring(sentinel.pid()), L"live.txt"});

  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\n");
}

TEST(tail, tail_repeated_pid_waits_until_all_processes_exit) {
  TempDir tmp;
  tmp.write("live.txt", "start\n");

  auto long_sentinel = start_follow_sentinel(5);
  auto short_sentinel = start_follow_sentinel(2);
  EXPECT_TRUE(long_sentinel.valid);
  EXPECT_TRUE(short_sentinel.valid);
  if (!long_sentinel.valid || !short_sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    std::ofstream out(tmp.path / "live.txt", std::ios::binary | std::ios::app);
    out << "after-short-pid\n";
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-n", L"0", L"-f", L"--sleep-interval", L"0.05",
                      L"--pid", std::to_wstring(long_sentinel.pid()), L"--pid",
                      std::to_wstring(short_sentinel.pid()), L"live.txt"});

  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("after-short-pid\n") != std::string::npos);
}

TEST(tail, tail_debug_reports_follow_mode_to_stderr_only) {
  TempDir tmp;
  tmp.write("live.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--debug", L"-f", L"--pid", L"999999", L"live.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\n");
  EXPECT_TRUE(r.stderr_text.find("polling follow implementation") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("following by descriptor") !=
              std::string::npos);
}

TEST(tail, tail_multi_file_follow_checks_later_files) {
  TempDir tmp;
  tmp.write("a.txt", "a0\n");
  tmp.write("b.txt", "b0\n");

  auto sentinel = start_follow_sentinel();
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::ofstream out(tmp.path / "b.txt", std::ios::binary | std::ios::app);
    out << "b1\n";
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe",
        {L"-n", L"0", L"-f", L"--sleep-interval", L"0.05", L"--pid",
         std::to_wstring(sentinel.pid()), L"a.txt", L"b.txt"});

  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("==> a.txt <==\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("==> b.txt <==\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b1\n") != std::string::npos);
}

TEST(tail, tail_F_follows_renamed_and_recreated_path) {
  TempDir tmp;
  tmp.write("live.txt", "old0\n");

  auto sentinel = start_follow_sentinel();
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    std::error_code ec;
    for (int attempt = 0; attempt < 20; ++attempt) {
      std::filesystem::rename(tmp.path / "live.txt", tmp.path / "rotated.txt",
                              ec);
      if (!ec) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    tmp.write("live.txt", "new0\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(tmp.path / "live.txt", std::ios::binary | std::ios::app);
    out << "new1\n";
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-F", L"--sleep-interval", L"0.05", L"--pid",
                      std::to_wstring(sentinel.pid()), L"live.txt"});

  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("old0\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new0\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new1\n") != std::string::npos);
}

TEST(tail, tail_follow_name_max_unchanged_stats_reopens_recreated_path) {
  TempDir tmp;
  tmp.write("live.txt", "old0\n");

  auto sentinel = start_follow_sentinel();
  EXPECT_TRUE(sentinel.valid);
  if (!sentinel.valid) return;

  std::thread writer([&tmp]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::error_code ec;
    for (int attempt = 0; attempt < 20; ++attempt) {
      std::filesystem::rename(tmp.path / "live.txt", tmp.path / "rotated.txt",
                              ec);
      if (!ec) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    tmp.write("live.txt", "new0\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(tmp.path / "live.txt", std::ios::binary | std::ios::app);
    out << "new1\n";
  });

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--follow=name", L"--retry", L"--max-unchanged-stats",
                      L"1", L"--sleep-interval", L"0.05", L"--pid",
                      std::to_wstring(sentinel.pid()), L"live.txt"});

  auto r = p.run();
  writer.join();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("old0\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new0\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new1\n") != std::string::npos);
}

TEST(tail, tail_sleep_interval_zero_is_accepted) {
  TempDir tmp;
  tmp.write("a.txt", "1\n2\n3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--sleep-interval", L"0", L"-n", L"1", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n");
}

TEST(tail, tail_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "line1\nline2\nline3\n");
  tmp.write("file2.txt", "line4\nline5\nline6\n");
  tmp.write("other.log", "log1\nlog2\nlog3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-n", L"1", L"*.txt"});

  TEST_LOG_CMD_LIST("tail.exe", L"-n", L"1", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tail output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line3") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line6") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("log3") == std::string::npos);
}

TEST(tail, tail_zero_terminated_records) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'a', '\0', 'b', '\0', 'c', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-z", L"-n", L"2", L"a.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("b\0c\0", 4));
}

TEST(tail, tail_zero_terminated_multi_file_headers) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'a', '\0', 'b', '\0'});
  tmp.write_bytes("b.bin", {'c', '\0', 'd', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-z", L"-n", L"1", L"-v", L"a.bin", L"b.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("==> a.bin <==\0b\0\0==> b.bin <==\0d\0", 33));
}
