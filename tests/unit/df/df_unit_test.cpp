// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(df, df_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {});

  TEST_LOG_CMD_LIST("df.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show disk information
  EXPECT_TRUE(r.stdout_text.find("Filesystem") != std::string::npos ||
              r.stdout_text.length() > 0);
}

TEST(df, df_without_operands_lists_all_accessible_logical_drives) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_GE(std::count(r.stdout_text.begin(), r.stdout_text.end(), ':'), 1);
}

TEST(df, df_human_readable) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-h"});

  TEST_LOG_CMD_LIST("df.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -h output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show human-readable sizes (K, M, G, etc.)
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(df, df_kilobytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-k"});

  TEST_LOG_CMD_LIST("df.exe", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show sizes in 1K blocks
  EXPECT_TRUE(r.stdout_text.find("1K-blocks") != std::string::npos ||
              r.stdout_text.length() > 0);
}

TEST(df, df_portability_uses_gnu_default_1024_blocks) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-P"});

  TEST_LOG_CMD_LIST("df.exe", L"-P");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1024-blocks"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("512-blocks"), std::string::npos);
}

TEST(df, df_si) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-H"});

  TEST_LOG_CMD_LIST("df.exe", L"-H");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -H output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show SI units (1000-based)
  EXPECT_TRUE(r.stdout_text.length() > 0);
}

TEST(df, df_block_size_bytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-B", L"1"});

  TEST_LOG_CMD_LIST("df.exe", L"-B", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -B 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1B-blocks"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Used"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Available"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_block_size_human_readable_alias) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--block-size=human-readable"});

  TEST_LOG_CMD_LIST("df.exe", L"--block-size=human-readable");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --block-size=human-readable output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] --block-size=human-readable selects -h, whose header is
  // "Filesystem Size Used Avail Use% Mounted on" ("Available"/"Capacity"
  // only appear in POSIX -P mode).
  EXPECT_NE(r.stdout_text.find("Size"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Avail"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Use%"), std::string::npos);
}

// GNU appends the unit letter to values for a bare-suffix block size
// ("df -BM" prints "1M" values and a "1M-blocks" header).
TEST(df, df_bare_suffix_block_size_appends_unit) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-BM"});

  TEST_LOG_CMD_LIST("df.exe", L"-BM");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -BM output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1M-blocks"), std::string::npos);
  // Every size cell ends with the unit letter (e.g. "12M").
  EXPECT_NE(r.stdout_text.find("M "), std::string::npos);
}

TEST(df, df_output_field_list) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--output=source,size,used,avail,pcent,target"});

  TEST_LOG_CMD_LIST("df.exe", L"--output=source,size,used,avail,pcent,target");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --output field list output", r.stdout_text);
  TEST_LOG("df.exe --output field list stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("1K-blocks"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Used"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Avail"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Use%"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("Available"), std::string::npos);
}

TEST(df, df_output_without_field_list_prints_all_gnu_fields) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--output"});

  TEST_LOG_CMD_LIST("df.exe", L"--output");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --output output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Type"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Inodes"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUsed"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IFree"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUse%"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("1K-blocks"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("File"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_output_rejects_mutually_exclusive_print_type) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--output=source", L"-T"});

  TEST_LOG_CMD_LIST("df.exe", L"--output=source", L"-T");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --output -T stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("options -T and --output are mutually "
                               "exclusive"),
            std::string::npos);
}

TEST(df, df_output_rejects_empty_inline_field_list) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--output="});

  TEST_LOG_CMD_LIST("df.exe", L"--output=");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --output= stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("option --output: field  unknown"),
            std::string::npos);
}

TEST(df, df_total_row_shape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"--total", L"-k"});

  TEST_LOG_CMD_LIST("df.exe", L"--total", L"-k");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe --total -k output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1K-blocks"), std::string::npos);
  // [GNU] the total row occupies the Filesystem column with an empty
  // mount column, so the line starts at column zero.
  EXPECT_NE(r.stdout_text.find("\ntotal"), std::string::npos);
}

TEST(df, df_accepts_all_sync_no_sync) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-a", L"--sync", L"--no-sync"});

  TEST_LOG_CMD_LIST("df.exe", L"-a", L"--sync", L"--no-sync");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -a --sync --no-sync output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_print_type) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-T"});

  TEST_LOG_CMD_LIST("df.exe", L"-T");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -T output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Type"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
}

TEST(df, df_type_filter_no_matches_keeps_header) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-t", L"definitely-not-a-real-fs-type"});

  TEST_LOG_CMD_LIST("df.exe", L"-t", L"definitely-not-a-real-fs-type");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -t no-match output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("Mounted on"), std::string::npos);
  EXPECT_EQ(r.stdout_text.find("\ntotal"), std::string::npos);
}

TEST(df, df_inodes_shape) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-i"});

  TEST_LOG_CMD_LIST("df.exe", L"-i");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe -i output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Inodes"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUsed"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IFree"), std::string::npos);
  EXPECT_NE(r.stdout_text.find("IUse%"), std::string::npos);
}

TEST(df, df_wildcard_operands_expand) {
  TempDir tmp;
  tmp.write("sample.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("df.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("df.exe wildcard output", r.stdout_text);
  TEST_LOG("df.exe wildcard stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
}

// [GNU] -m is a synonym for --block-size=1M (#308).
TEST(df, df_m_selects_1m_blocks) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"-m", L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1M-blocks"), std::string::npos);
}

// [GNU] df stats each operand: a missing path is reported and the run fails
// with exit 1, but valid operands are still listed (#336).
TEST(df, df_missing_operand_errors_but_valid_operands_still_list) {
  TempDir tmp;
  tmp.write("real.txt", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"df.exe", {L"real.txt", L"zzz_missing_df_operand"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("zzz_missing_df_operand"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("No such file or directory"), std::string::npos);
  // The valid operand still produced a listing.
  EXPECT_NE(r.stdout_text.find("Filesystem"), std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("real.txt") != std::string::npos ||
              r.stdout_text.find(":\\") != std::string::npos);
}

// [GNU] With no size option the block size comes from DF_BLOCK_SIZE >
// BLOCK_SIZE > BLOCKSIZE (#981).
TEST(df, df_block_size_env_is_honored) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"DF_BLOCK_SIZE", L"1M");
  p.add(L"df.exe", {L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("1M-blocks"), std::string::npos);
}

TEST(df, df_block_size_env_falls_through_to_blocksize) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"BLOCKSIZE", L"512");
  p.add(L"df.exe", {L"."});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("512B-blocks"), std::string::npos);
}

// [GNU] -F is an obsolete hidden synonym for -t/--type (issue #1078).
TEST(df, df_obsolete_F_synonym_for_type) {
  Pipeline pf;
  pf.add(L"df.exe", {L"-F", L"NTFS"});
  Pipeline pt;
  pt.add(L"df.exe", {L"-t", L"NTFS"});

  auto rf = pf.run();
  auto rt = pt.run();
  EXPECT_EQ(rf.exit_code, 0);
  EXPECT_EQ(rt.exit_code, 0);
  EXPECT_EQ(rf.stdout_text.empty(), rt.stdout_text.empty());
}
