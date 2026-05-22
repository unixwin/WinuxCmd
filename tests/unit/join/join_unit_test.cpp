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
 *  - File: join_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(join, join_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "  1   apple green\n2\tbanana\n3 cherry\n");
  tmp.write("file2.txt", "1 red\n2 yellow\n3 red\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text,
            "1 apple green red\n2 banana yellow\n3 cherry red\n");
}

TEST(join, join_single_file) {
  TempDir tmp;
  tmp.write("file1.txt", "1 apple\n2 banana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"file1.txt", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should match with itself
  EXPECT_TRUE(r.stdout_text.find("1 apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 banana") != std::string::npos);
}

TEST(join, join_outputs_unpaired_lines_with_auto_format) {
  TempDir tmp;
  tmp.write("file1.txt", "k a b\nu only\n");
  tmp.write("file2.txt", "k x\nv y z\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-a", L"1", L"-a", L"2", L"-e", L"NA", L"-o", L"auto",
                      L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "k a b x\nu only NA NA\nv NA NA y\n");
}

TEST(join, join_custom_output_and_only_unpaired) {
  TempDir tmp;
  tmp.write("left.csv", "id,name,role\n1,Ada,\n2,Ben,ops\n");
  tmp.write("right.csv", "id,team\n1,core\n3,infra\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-t", L",", L"-v", L"1", L"-e", L"NA", L"-o",
                      L"0,1.2,2.2", L"left.csv", L"right.csv"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2,Ben,NA\n");
}

TEST(join, join_ignore_case) {
  TempDir tmp;
  tmp.write("file1.txt", "Key left\n");
  tmp.write("file2.txt", "key right\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-i", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "Key left right\n");
}

TEST(join, join_header_drives_auto_output_fields) {
  TempDir tmp;
  tmp.write("file1.txt", "id name\n1 Ada\n2 Ben Smith\n");
  tmp.write("file2.txt", "id team\n1 core\n3 infra\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"--header", L"-a", L"1", L"-a", L"2", L"-e", L"NA",
                      L"-o", L"auto", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "id name team\n1 Ada core\n2 Ben NA\n3 NA infra\n");
}

TEST(join, join_zero_terminated_records) {
  TempDir tmp;
  tmp.write("file1.bin", std::string("1 a\0x left\0", 11));
  tmp.write("file2.bin", std::string("1 b\0x right\0", 12));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"join.exe", {L"-z", L"file1.bin", L"file2.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("1 a b\0x left right\0", 19));
}
