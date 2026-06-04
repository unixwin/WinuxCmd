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
 *  - File: sort_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(sort, sort_basic_lexicographic) {
  TempDir tmp;
  tmp.write("a.txt", "pear\napple\nbanana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "apple\nbanana\npear\n");
}

TEST(sort, sort_numeric_reverse_unique) {
  TempDir tmp;
  tmp.write("n.txt", "2\n10\n2\n1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-n", L"-r", L"-u", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "10\n2\n1\n");
}

TEST(sort, sort_numeric_unique_uses_numeric_key_equality) {
  TempDir tmp;
  tmp.write("n.txt", "1 b\n1 a\n2 z\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-n", L"-u", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1 b\n2 z\n");
}

TEST(sort, sort_stable_numeric_unique_keeps_first_equal_key) {
  TempDir tmp;
  tmp.write("n.txt", "1 b\n1 a\n2 z\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-n", L"-u", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1 b\n2 z\n");
}

TEST(sort, sort_ignore_case_and_key) {
  TempDir tmp;
  tmp.write("k.txt", "b 2\nA 3\na 1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-f", L"-k", L"1,1", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "A 3\na 1\nb 2\n");
}

TEST(sort, sort_key_unique_uses_key_equality) {
  TempDir tmp;
  tmp.write("k.txt", "b 2\nb 1\na 9\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-k", L"1,1", L"-u", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a 9\nb 2\n");
}

TEST(sort, sort_repeated_key_options_compare_in_order) {
  TempDir tmp;
  tmp.write("k.txt", "b 2\nb 1\na 9\na 8\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-k", L"1,1", L"-k", L"2,2n", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a 8\na 9\nb 1\nb 2\n");
}

TEST(sort, sort_repeated_long_key_options_compare_in_order) {
  TempDir tmp;
  tmp.write("k.txt", "beta 10\nalpha 2\nalpha 1\nbeta 2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--key=1,1", L"--key=2,2n", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha 1\nalpha 2\nbeta 2\nbeta 10\n");
}

TEST(sort, sort_debug_reports_key_without_changing_stdout) {
  TempDir tmp;
  tmp.write("k.txt", "beta 10\nalpha 2\nalpha 1\nbeta 2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--debug", L"--key=2,2n", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha 1\nalpha 2\nbeta 2\nbeta 10\n");
  EXPECT_TRUE(r.stderr_text.find("sort: debug") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("key 1") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("numeric") != std::string::npos);
}

TEST(sort, sort_key_range_limits_end_field) {
  TempDir tmp;
  tmp.write("k.txt", "x,a,2\ny,a,1\nz,b,0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-t,", L"-k", L"2,2", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x,a,2\ny,a,1\nz,b,0\n");
}

TEST(sort, sort_field_separator_accepts_nul_escape) {
  TempDir tmp;
  tmp.write_bytes("nul-fields.txt",
                  {'b', '\0', 'y', '\n', 'a', '\0', 'x', '\n'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-t", L"\\0", L"-k", L"2,2", L"nul-fields.txt"});
  auto r = p.run();

  std::string expected;
  expected.push_back('a');
  expected.push_back('\0');
  expected.push_back('x');
  expected.push_back('\n');
  expected.push_back('b');
  expected.push_back('\0');
  expected.push_back('y');
  expected.push_back('\n');

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(sort, sort_open_ended_key_extends_to_line_end) {
  TempDir tmp;
  tmp.write("k.txt", "x,a,2\ny,a,1\nz,b,0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-t,", L"-k", L"2", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "y,a,1\nx,a,2\nz,b,0\n");
}

TEST(sort, sort_key_character_positions) {
  TempDir tmp;
  tmp.write("k.txt", "aa2\naa1\nab0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-k", L"1.3,1.3", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab0\naa1\naa2\n");
}

TEST(sort, sort_ignore_leading_blanks_affects_key_character_positions) {
  TempDir tmp;
  tmp.write("k.txt", "  a2\n  b1\n a0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-b", L"-s", L"-k", L"1.2,1.2", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, " a0\n  b1\n  a2\n");
}

TEST(sort, sort_key_end_character_zero_means_field_end) {
  TempDir tmp;
  tmp.write("k.txt", "a 10\nb 02\na 02\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-k", L"2.1,2.0n", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b 02\na 02\na 10\n");
}

TEST(sort, sort_output_file_option) {
  TempDir tmp;
  tmp.write("in.txt", "z\nx\ny\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-o", L"out.txt", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("out.txt"), "x\ny\nz\n");
}

TEST(sort, sort_accepts_buffer_size_hint) {
  TempDir tmp;
  tmp.write("in.txt", "z\nx\ny\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-S10K", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "x\ny\nz\n");
}

TEST(sort, sort_accepts_long_buffer_size_percent_hint) {
  TempDir tmp;
  tmp.write("in.txt", "3\n1\n2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--buffer-size=50%", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3\n");
}

TEST(sort, sort_rejects_invalid_buffer_size_hint) {
  Pipeline p;
  p.add(L"sort.exe", {L"--buffer-size=bad"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("invalid buffer size") != std::string::npos);
}

TEST(sort, sort_rejects_empty_buffer_size_hint) {
  Pipeline p;
  p.add(L"sort.exe", {L"-S", L""});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("invalid buffer size") != std::string::npos);
}

TEST(sort, sort_version_sort) {
  TempDir tmp;
  tmp.write("v.txt", "1.2.10\n1.2.2\n1.10.0\n1.2.0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-V", L"v.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.2.0\n1.2.2\n1.2.10\n1.10.0\n");
}

TEST(sort, sort_long_sort_numeric_word) {
  TempDir tmp;
  tmp.write("n.txt", "10\n2\n1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--sort=numeric", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n10\n");
}

TEST(sort, sort_numeric_does_not_accept_plus_or_exponent) {
  TempDir tmp;
  tmp.write("n.txt", "+1\n1e2\n2\n1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-n", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "+1\n1\n1e2\n2\n");
}

TEST(sort, sort_long_sort_general_numeric_word) {
  TempDir tmp;
  tmp.write("g.txt", "1e2\n20\n3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--sort=general-numeric", L"g.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n20\n1e2\n");
}

TEST(sort, sort_long_sort_human_numeric_word) {
  TempDir tmp;
  tmp.write("h.txt", "1M\n100\n2K\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--sort=human-numeric", L"h.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "100\n2K\n1M\n");
}

TEST(sort, sort_human_numeric_uses_suffix_order_before_value) {
  TempDir tmp;
  tmp.write("h.txt", "2000M\n1G\n999K\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-h", L"h.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "999K\n2000M\n1G\n");
}

TEST(sort, sort_human_numeric_compares_sign_before_suffix) {
  TempDir tmp;
  tmp.write("h.txt", "1K\n0Q\n-1G\n1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-h", L"h.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-1G\n0Q\n1\n1K\n");
}

TEST(sort, sort_long_sort_month_alias) {
  TempDir tmp;
  tmp.write("m.txt", "Dec\nJan\nApr\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--sort=M", L"m.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Jan\nApr\nDec\n");
}

TEST(sort, sort_ordering_mode_last_option_wins) {
  TempDir tmp;
  tmp.write("v.txt", "1.10\n1.2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--sort=numeric", L"--sort=version", L"v.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.2\n1.10\n");
}

TEST(sort, sort_short_ordering_mode_last_option_wins) {
  TempDir tmp;
  tmp.write("v.txt", "1.10\n1.2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-V", L"-n", L"v.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.10\n1.2\n");
}

TEST(sort, sort_rejects_invalid_long_sort_word) {
  Pipeline p;
  p.add(L"sort.exe", {L"--sort=bad"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stderr_text.find("invalid sort mode") != std::string::npos);
}

TEST(sort, sort_month_sort) {
  TempDir tmp;
  tmp.write("m.txt", "Dec\n feb\nJan\nunknown\nApr\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-M", L"m.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "unknown\nJan\n feb\nApr\nDec\n");
}

TEST(sort, sort_month_sort_reverse) {
  TempDir tmp;
  tmp.write("m.txt", "Dec\nFeb\njan\nunknown\nApr\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--month-sort", L"-r", L"m.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "Dec\nApr\nFeb\njan\nunknown\n");
}

TEST(sort, sort_dictionary_order_ignores_punctuation) {
  TempDir tmp;
  tmp.write("d.txt", "a#z\naa\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-d", L"d.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aa\na#z\n");
}

TEST(sort, sort_ignore_nonprinting_filters_control_chars) {
  TempDir tmp;
  std::string input = "a";
  input.push_back('\x01');
  input += "z\naa\n";
  tmp.write("i.txt", input);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-i", L"i.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::string expected = "aa\na";
  expected.push_back('\x01');
  expected += "z\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(sort, sort_stable_preserves_equal_key_order) {
  TempDir tmp;
  tmp.write("s.txt", "b 2\nb 1\na 1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-s", L"-k", L"1,1", L"s.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a 1\nb 2\nb 1\n");
}

TEST(sort, sort_random_sort_uses_source_and_groups_equal_keys) {
  TempDir tmp;
  tmp.write("seed.bin", "fixed random source\n");
  tmp.write("r.txt", "alpha 1\nbravo 1\nalpha 2\ncharlie 1\nbravo 2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe",
        {L"-R", L"--random-source=seed.bin", L"-k", L"1,1", L"r.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "charlie 1\nbravo 1\nbravo 2\nalpha 1\nalpha 2\n");
}

TEST(sort, sort_random_sort_reverse_writes_output_file) {
  TempDir tmp;
  tmp.write("seed.bin", "fixed random source\n");
  tmp.write("r.txt", "alpha\nbravo\ncharlie\ndelta\necho\nfoxtrot\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--random-sort", L"--random-source", L"seed.bin", L"-r",
                      L"-o", L"out.txt", L"r.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("out.txt"),
                 "echo\ndelta\nfoxtrot\nalpha\nbravo\ncharlie\n");
}

TEST(sort, sort_merge_sorted_inputs) {
  TempDir tmp;
  tmp.write("a.txt", "a\nc\n");
  tmp.write("b.txt", "b\nd\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-m", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\nd\n");
}

TEST(sort, sort_check_detects_unsorted_input) {
  TempDir tmp;
  tmp.write("bad.txt", "b\na\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-c", L"bad.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sort, sort_check_quiet_detects_unsorted_input) {
  TempDir tmp;
  tmp.write("bad.txt", "b\na\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-C", L"bad.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(sort, sort_check_unique_rejects_adjacent_equal_keys) {
  TempDir tmp;
  tmp.write("bad.txt", "1 b\n1 a\n2 z\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-c", L"-n", L"-u", L"bad.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sort, sort_files0_from_reads_nul_terminated_names) {
  TempDir tmp;
  tmp.write("a.txt", "c\na\n");
  tmp.write("b.txt", "b\nd\n");
  tmp.write_bytes("list.bin", {'a', '.', 't', 'x', 't', '\0', 'b', '.', 't',
                               'x', 't', '\0'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"--files0-from", L"list.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\nd\n");
}

TEST(sort, sort_uniq_pipeline_accepts_utf16le_stdin_with_bom) {
  // Simulate PowerShell native pipeline output with UTF-16LE and per-line BOM.
  std::u16string u16 = u"\ufeffdog\r\n\ufeffdog\r\n\ufeffcat\r\n";
  std::string bytes;
  bytes.reserve(u16.size() * 2);
  for (char16_t ch : u16) {
    bytes.push_back(static_cast<char>(ch & 0xFF));
    bytes.push_back(static_cast<char>((ch >> 8) & 0xFF));
  }

  Pipeline p;
  p.set_stdin(bytes);
  p.add(L"sort.exe", {});
  p.add(L"uniq.exe", {L"-c"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1 cat") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 dog") != std::string::npos);
}

TEST(sort, sort_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "cherry\napple\n");
  tmp.write("file2.txt", "banana\ndate\n");
  tmp.write("other.log", "zzz\naaa\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("sort.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sort output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain content from .txt files but not .log
  EXPECT_TRUE(r.stdout_text.find("apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cherry") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("date") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("aaa") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("zzz") == std::string::npos);
}
