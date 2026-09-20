// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

TEST(split, split_number_line_mode_preserves_crlf_records) {
  TempDir tmp;
  {
    std::ofstream out(tmp.path / "input.txt", std::ios::binary);
    out << "a\r\nb\r\nc\r\n";
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"l/2", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "a\r\n");
  EXPECT_EQ_TEXT(tmp.read("partab"), "b\r\nc\r\n");
}

TEST(split, split_number_mode_reassembles_without_overlap) {
  TempDir tmp;
  const std::string input = "line01\nline02\nline03\nline04\nline05\n";
  tmp.write("input.txt", input);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"4", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::string rebuilt;
  for (const char* suffix : {"aa", "ab", "ac", "ad"}) {
    rebuilt += tmp.read(std::string("part") + suffix);
  }
  EXPECT_EQ(rebuilt, input);
}

TEST(split, split_number_k_of_n_mode_writes_selected_chunk_to_stdout) {
  TempDir tmp;
  tmp.write("input.txt", "abcdefghi");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"2/3", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "def");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "partaa"));
}

TEST(split, split_number_line_k_of_n_mode_preserves_records_to_stdout) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\nc\nd\ne\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"l/2/3", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\nc\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "partaa"));
}

TEST(split, split_number_round_robin_mode_writes_each_output_file) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\nc\nd\ne\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"r/2", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "a\nc\ne\n");
  EXPECT_EQ_TEXT(tmp.read("partab"), "b\nd\n");
}

TEST(split, split_number_round_robin_k_of_n_mode_writes_selected_records) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\nc\nd\ne\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-n", L"r/2/3", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "b\ne\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "partaa"));
}

TEST(split, split_elide_empty_files_keeps_suffixes_dense) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-e", L"-n", L"l/5", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "partaa"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "partab"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "partac"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "partad"));
  EXPECT_EQ_TEXT(tmp.read("partaa"), "a\n");
  EXPECT_EQ_TEXT(tmp.read("partab"), "b\n");
}

TEST(split, split_separator_splits_records_without_newlines) {
  TempDir tmp;
  tmp.write("input.txt", "aa|bbbb|cc|");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-t", L"|", L"-l", L"2", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "aa|bbbb|");
  EXPECT_EQ_TEXT(tmp.read("partab"), "cc|");
}

TEST(split, split_rejects_empty_separator) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-t", L"", L"-l", L"1", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("split: invalid separator") !=
              std::string::npos);
}

TEST(split, split_filter_exports_file_environment_variable) {
  TempDir tmp;
  tmp.write("input.txt", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(
      L"split.exe",
      {L"--filter=powershell -NoProfile -Command "
       L"\"$out=[System.IO.File]::Open($env:FILE,[System.IO.FileMode]::Create,"
       L"[System.IO.FileAccess]::Write);[Console]::OpenStandardInput().CopyTo("
       L"$out);$out.Dispose()\"",
       L"-l", L"1", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "a\n");
  EXPECT_EQ_TEXT(tmp.read("partab"), "b\n");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "%FILE%"));
}

TEST(split, split_verbose_reports_created_files) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"--verbose", L"-l", L"1", L"input.txt", L"part"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stderr_text,
            "creating file 'partaa'\n"
            "creating file 'partab'\n"
            "creating file 'partac'\n");
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

TEST(split, split_invalid_size_messages_quote_argument) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\n");

  // GNU strtoint_die quotes the rejected argument for every size option.
  const wchar_t* flag_sets[][2] = {
      {L"-l", L"0"},   {L"-l", L"x"},   {L"-l", L"2fb"}, {L"-b", L"0"},
      {L"-b", L"abc"}, {L"-C", L"0"},   {L"-n", L"0"},   {L"-n", L"x"},
      {L"-n", L"1/0"}, {L"-n", L"0/3"}, {L"-n", L"a/3"}, {L"-n", L"1/2/3"},
  };
  const char* expected[] = {
      "split: invalid number of lines: '0'\n",
      "split: invalid number of lines: 'x'\n",
      "split: invalid number of lines: '2fb'\n",
      "split: invalid number of bytes: '0'\n",
      "split: invalid number of bytes: 'abc'\n",
      "split: invalid number of lines: '0'\n",  // GNU -C says "lines"
      "split: invalid number of chunks: '0'\n",
      "split: invalid number of chunks: 'x'\n",
      "split: invalid number of chunks: '0'\n",
      "split: invalid chunk number: '0'\n",
      "split: invalid number of chunks: 'a/3'\n",
      "split: invalid number of chunks: '2/3'\n",
  };
  for (size_t i = 0; i < sizeof(flag_sets) / sizeof(flag_sets[0]); ++i) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"split.exe",
          {flag_sets[i][0], flag_sets[i][1], L"input.txt", L"part"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_EQ_TEXT(r.stderr_text, expected[i]);
  }

  // k/N with a valid split still works.
  Pipeline good;
  good.set_cwd(tmp.wpath());
  good.add(L"split.exe", {L"-n", L"1/2", L"input.txt", L"part"});
  EXPECT_EQ(good.run().exit_code, 0);
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

TEST(split, split_rejects_extra_operand_with_help_hint) {
  TempDir tmp;
  tmp.write("input.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"input.txt", L"part", L"extra"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("split extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "split: extra operand 'extra'\n"
            "Try 'split --help' for more information.\n");
}

TEST(split, split_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "split: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(split, split_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "split: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

// [GNU] obsolete "-NUMBER" is accepted as "-l NUMBER" (issue #1081).
TEST(split, split_obsolete_dash_number_lines) {
  TempDir tmp;
  tmp.write("input.txt", "l1\nl2\nl3\nl4\nl5\nl6\nl7\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-3", L"input.txt", L"part"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "l1\nl2\nl3\n");
  EXPECT_EQ_TEXT(tmp.read("partab"), "l4\nl5\nl6\n");
  EXPECT_EQ_TEXT(tmp.read("partac"), "l7\n");
}

// [GNU] hidden ---io-blksize=SIZE is accepted and its size is validated
// (issue #1081).
TEST(split, split_hidden_io_blksize_accepted_and_validated) {
  TempDir tmp;
  tmp.write("input.txt", "l1\nl2\nl3\nl4\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe",
        {L"---io-blksize=4096", L"-l", L"2", L"input.txt", L"part"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("partaa"), "l1\nl2\n");

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.add(L"split.exe", {L"---io-blksize=bad", L"input.txt", L"q"});
  auto rb = bad.run();
  EXPECT_EQ(rb.exit_code, 1);
  EXPECT_TRUE(rb.stderr_text.find("invalid IO block size") !=
              std::string::npos);
}

// [GNU] obsolete -NUMBER conflicts with an explicit split mode like -l.
TEST(split, split_obsolete_dash_number_conflicts_with_lines) {
  TempDir tmp;
  tmp.write("input.txt", "l1\nl2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"split.exe", {L"-3", L"-l", L"5", L"input.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot split in more than one way") !=
              std::string::npos);
}
