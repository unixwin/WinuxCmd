/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
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
 *  - File: split_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(split, split_by_lines) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\nline3\nline4\nline5\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-l", L"2", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Check that split files were created
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "partaa") ||
              std::filesystem::exists(tmp.path / "partaa.txt"));
}

TEST(split, split_by_bytes) {
  TempDir tmp;
  tmp.write("input.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-b", L"5", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "partaa") ||
              std::filesystem::exists(tmp.path / "partaa.txt"));
}

TEST(split, split_numeric_suffix_start_and_additional_suffix) {
  TempDir tmp;
  tmp.write("input.txt", "abcdefghij");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-b", L"5", L"--numeric-suffixes=8",
                       L"--additional-suffix=.part", L"input.txt", L"seg-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("seg-08.part"), "abcde");
  EXPECT_EQ_TEXT(tmp.read("seg-09.part"), "fghij");
}

TEST(split, split_hex_suffixes) {
  TempDir tmp;
  tmp.write("input.txt", "abcdefghijkl");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-b", L"4", L"-x", L"-a", L"3", L"input.txt", L"hex-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("hex-000"), "abcd");
  EXPECT_EQ_TEXT(tmp.read("hex-001"), "efgh");
  EXPECT_EQ_TEXT(tmp.read("hex-002"), "ijkl");
}

TEST(split, split_line_bytes_preserves_complete_lines) {
  TempDir tmp;
  tmp.write("input.txt", "aa\nbbbb\ncc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-C", L"6", L"input.txt", L"chunk"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("chunkaa"), "aa\n");
  EXPECT_EQ_TEXT(tmp.read("chunkab"), "bbbb\n");
  EXPECT_EQ_TEXT(tmp.read("chunkac"), "cc\n");
}

TEST(split, split_rejects_multiple_split_modes) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-l", L"1", L"-b", L"1", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("cannot split in more than one way") !=
                  std::string::npos ||
              r.stderr_text.find("cannot split in more than one way") !=
                  std::string::npos);
}

TEST(split, split_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"split.exe", {L"-l", L"1", L"-", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(split, split_omitted_input_reads_stdin) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("line1\nline2\n");
  p.add(L"split.exe", {L"-l", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("xaa"), "line1\n");
  EXPECT_EQ_TEXT(tmp.read("xab"), "line2\n");
}

TEST(split, split_wildcard_input_rejects_ambiguous_match) {
  TempDir tmp;
  tmp.write("a.txt", "line1\n");
  tmp.write("b.txt", "line2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-l", L"1", L"*.txt", L"part"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("exactly one file") != std::string::npos ||
              r.stderr_text.find("exactly one file") != std::string::npos);
}
