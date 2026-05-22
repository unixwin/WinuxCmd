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
 *  - File: head_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include <array>
#include <string_view>

#include "framework/winuxtest.h"

TEST(head, head_default_first_10_lines) {
  TempDir tmp;
  tmp.write("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");
}

TEST(head, head_n_and_c_options) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"head.exe", {L"-n", L"2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "alpha\nbeta\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"head.exe", {L"-c", L"5", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "alpha");
}

TEST(head, head_last_count_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"head.exe", {L"-c", L"2", L"-n", L"1", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "alpha\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"head.exe", {L"-n", L"1", L"-c", L"2", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "al");
}

TEST(head, head_negative_line_and_byte_counts) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"head.exe", {L"--lines=-1", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "alpha\nbeta\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"head.exe", {L"-c-6", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "alpha\nbeta\n");
}

TEST(head, head_count_suffixes) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-c", L"1K", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0123456789abcdef");
}

TEST(head, head_KB_suffix_is_decimal_bytes) {
  TempDir tmp;
  std::string data(1200, 'x');
  tmp.write("a.txt", data);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-c", L"1KB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text.size(), 1000u);
}

TEST(head, head_rejects_lowercase_kB_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-c", L"1kB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid") != std::string::npos);
}

TEST(head, head_accepts_extended_representable_count_suffixes) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  const std::array<std::wstring_view, 9> suffixes{
      L"T", L"TB", L"TiB", L"P", L"PB", L"PiB", L"E", L"EB", L"EiB"};
  for (auto suffix : suffixes) {
    std::wstring count = L"1";
    count.append(suffix.data(), suffix.size());

    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"head.exe", {L"-c", count, L"a.txt"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "0123456789abcdef");
  }
}

TEST(head, head_accepts_zero_with_oversized_count_suffixes) {
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
    p.add(L"head.exe", {L"-c", count, L"a.txt"});
    auto r = p.run();

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(r.stdout_text, "");
  }
}

TEST(head, head_rejects_nonzero_oversized_count_suffix) {
  TempDir tmp;
  tmp.write("a.txt", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-c", L"1QiB", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid") != std::string::npos);
}

TEST(head, head_legacy_count_shorthand) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-2", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nbeta\n");
}

TEST(head, head_obsolete_compact_byte_count) {
  TempDir tmp;
  tmp.write("a.txt", "abcdef\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-2c", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab");
}

TEST(head, head_obsolete_block_and_kilo_suffixes) {
  TempDir tmp;
  std::string data(1200, 'x');
  data.replace(0, 4, "head");
  tmp.write("a.txt", data);

  Pipeline block;
  block.set_cwd(tmp.wpath());
  block.add(L"head.exe", {L"-1b", L"a.txt"});
  auto block_result = block.run();

  EXPECT_EQ(block_result.exit_code, 0);
  EXPECT_EQ(block_result.stdout_text.size(), 512u);
  EXPECT_EQ_TEXT(block_result.stdout_text.substr(0, 4), "head");

  Pipeline kilo;
  kilo.set_cwd(tmp.wpath());
  kilo.add(L"head.exe", {L"-1k", L"a.txt"});
  auto kilo_result = kilo.run();

  EXPECT_EQ(kilo_result.exit_code, 0);
  EXPECT_EQ(kilo_result.stdout_text.size(), 1024u);
  EXPECT_EQ_TEXT(kilo_result.stdout_text.substr(0, 4), "head");
}

TEST(head, head_obsolete_compact_header_flags) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\nA3\n");
  tmp.write("b.txt", "B1\nB2\nB3\n");

  Pipeline quiet;
  quiet.set_cwd(tmp.wpath());
  quiet.add(L"head.exe", {L"-2q", L"a.txt", L"b.txt"});
  auto quiet_result = quiet.run();

  EXPECT_EQ(quiet_result.exit_code, 0);
  EXPECT_EQ_TEXT(quiet_result.stdout_text, "A1\nA2\nB1\nB2\n");

  Pipeline verbose;
  verbose.set_cwd(tmp.wpath());
  verbose.add(L"head.exe", {L"-1v", L"a.txt"});
  auto verbose_result = verbose.run();

  EXPECT_EQ(verbose_result.exit_code, 0);
  EXPECT_EQ_TEXT(verbose_result.stdout_text, "==> a.txt <==\nA1\n");
}

TEST(head, head_verbose_header_multi_files) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\n");
  tmp.write("b.txt", "B1\nB2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-n", L"1", L"-v", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("==> a.txt <==") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("==> b.txt <==") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("A1\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("B1\n") != std::string::npos);
}

TEST(head, head_last_header_option_wins) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\n");
  tmp.write("b.txt", "B1\nB2\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"head.exe", {L"-v", L"-q", L"-n", L"1", L"a.txt", L"b.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "A1\nB1\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"head.exe", {L"-q", L"-v", L"-n", L"1", L"a.txt", L"b.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "==> a.txt <==\nA1\n\n==> b.txt <==\nB1\n");
}

TEST(head, head_stdin_header_uses_standard_input) {
  TempDir tmp;
  tmp.write("a.txt", "A1\nA2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-n", L"1", L"-", L"a.txt"});
  p.set_stdin("S1\nS2\n");
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "==> standard input <==\nS1\n\n==> a.txt <==\nA1\n");
}

TEST(head, head_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "line1\nline2\nline3\n");
  tmp.write("file2.txt", "line4\nline5\nline6\n");
  tmp.write("other.log", "log1\nlog2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-n", L"1", L"*.txt"});

  TEST_LOG_CMD_LIST("head.exe", L"-n", L"1", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("head output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line4") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("log1") == std::string::npos);
}

TEST(head, head_zero_terminated_records) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'a', '\0', 'b', '\0', 'c', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-z", L"-n", L"2", L"a.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a\0b\0", 4));
}

TEST(head, head_zero_terminated_multi_file_headers) {
  TempDir tmp;
  tmp.write_bytes("a.bin", {'a', '\0', 'b', '\0'});
  tmp.write_bytes("b.bin", {'c', '\0', 'd', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"head.exe", {L"-z", L"-n", L"1", L"-v", L"a.bin", L"b.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            std::string("==> a.bin <==\0a\0\0==> b.bin <==\0c\0", 33));
}
