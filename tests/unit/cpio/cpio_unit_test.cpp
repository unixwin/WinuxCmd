/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(cpio, cpio_basic) {
  // [#1462] Names that do not exist are per-file errors in GNU cpio
  // ("cpio: <name>: No such file or directory") and the run fails;
  // the historical build warned and still exited 0.
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"cpio.exe", {L"-o"});

  TEST_LOG_CMD_LIST("cpio.exe", L"-o");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("hello"), std::string::npos);
}

TEST(cpio, cpio_newc_create_and_list_roundtrip) {
  TempDir tmp;
  tmp.write("cat-no-newline.txt", "tail");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("cat-no-newline.txt\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  // [GNU] A completed copy-out reports "N block(s)" on stderr.
  EXPECT_NE(created.stderr_text.find("block"), std::string::npos);
  EXPECT_TRUE(created.stdout_text.starts_with("070701"));
  EXPECT_NE(created.stdout_text.find("cat-no-newline.txt"), std::string::npos);
  EXPECT_NE(created.stdout_text.find("TRAILER!!!"), std::string::npos);

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  // [GNU] A plain table of contents lists member names only.
  EXPECT_EQ_TEXT(listed.stdout_text, "cat-no-newline.txt\n");
}

// [#1422] -F/--file names the archive file (an argv operand, resolved
// through the shared operand boundary): create writes the archive to the
// file instead of stdout, and list reads it back from the file.
TEST(cpio, cpio_file_option_create_and_list_issue1422) {
  TempDir tmp;
  tmp.write("f.txt", "data\n");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("f.txt\n");
  create.add(L"cpio.exe", {L"-o", L"-F", L"arc.cpio"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_TRUE(created.stdout_text.empty());
  EXPECT_TRUE(tmp.read("arc.cpio").starts_with("070701"));

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.add(L"cpio.exe", {L"-t", L"-F", L"arc.cpio"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_EQ_TEXT(listed.stdout_text, "f.txt\n");
}

// [#1422] A missing -F archive is an error, not an empty archive read
// from stdin.
TEST(cpio, cpio_file_option_missing_archive_issue1422) {
  TempDir tmp;

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.add(L"cpio.exe", {L"-t", L"-F", L"nope.cpio"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 1);
  EXPECT_NE(listed.stderr_text.find("cannot open archive"),
            std::string::npos);
}

// [#1422] Member names go through the shared operand boundary: a member
// named "name." (trailing dot) reads and restores the on-disk "name."
// file, never the DOS-stripped sibling "name" (the #1332 shape, on both
// the create read side and the extract write side).
TEST(cpio, cpio_trailing_dot_member_roundtrip_issue1422) {
  TempDir src;
  src.write("name", "content-B\n");
  src.write("name.", "content-A\n");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("name.\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_NE(created.stderr_text.find("block"), std::string::npos);

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i"});
  auto extracted = extract.run();

  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("name."), "content-A\n");
  EXPECT_EQ_TEXT(dst.read("name"), "");
}

// [#1422] A member named "NUL.txt" (DOS device stem with an extension)
// names the on-disk file on both sides, never the NUL device (the #1333
// shape).
TEST(cpio, cpio_nul_txt_member_roundtrip_issue1422) {
  TempDir src;
  src.write("NUL.txt", "real-content\n");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("NUL.txt\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_NE(created.stderr_text.find("block"), std::string::npos);

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i"});
  auto extracted = extract.run();

  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("NUL.txt"), "real-content\n");
}

// [#1462] Empty files and directories are real members: the historical
// build dropped both from archives (a failed/empty read was treated as
// "cannot read file") and never recreated them on extraction.
TEST(cpio, cpio_empty_file_and_directory_roundtrip_issue1462) {
  TempDir src;
  src.write("empty.txt", "");
  src.mkdir("subdir");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("empty.txt\nsubdir\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_TRUE(created.stdout_text.starts_with("070701"));

  Pipeline list;
  list.set_cwd(src.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_EQ_TEXT(listed.stdout_text, "empty.txt\nsubdir\n");

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i"});
  auto extracted = extract.run();

  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("empty.txt"), "");
  EXPECT_TRUE(
      std::filesystem::is_directory(std::filesystem::path(dst.wpath()) /
                                    "subdir"));
}

// [#1462] -H crc writes the 070702 magic with a real data checksum, and
// extraction verifies it (GNU: "%s: checksum error (0x%x, should be
// 0x%x)").
TEST(cpio, cpio_crc_roundtrip_and_checksum_issue1462) {
  TempDir src;
  src.write("payload.txt", "checksum-me\n");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("payload.txt\n");
  create.add(L"cpio.exe", {L"-o", L"-H", L"crc"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_TRUE(created.stdout_text.starts_with("070702"));

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i"});
  auto extracted = extract.run();

  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("payload.txt"), "checksum-me\n");
  EXPECT_EQ(extracted.stderr_text.find("checksum error"),
            std::string::npos);

  // Corrupt one payload byte: extraction must report the mismatch.
  std::string damaged = created.stdout_text;
  auto at = damaged.find("checksum-me");
  ASSERT_TRUE(at != std::string::npos);
  damaged[at] = 'X';

  TempDir dst2;
  Pipeline extract2;
  extract2.set_cwd(dst2.wpath());
  extract2.set_stdin(damaged);
  extract2.add(L"cpio.exe", {L"-i"});
  auto extracted2 = extract2.run();

  EXPECT_NE(extracted2.stderr_text.find("checksum error"),
            std::string::npos);
}

// [#1462] -H is validated against the GNU format name set; an unknown
// name is a usage error, and a known-but-unwritable format is refused
// instead of silently writing newc under its label.
TEST(cpio, cpio_format_validation_issue1462) {
  Pipeline bogus;
  bogus.set_stdin("x\n");
  bogus.add(L"cpio.exe", {L"-o", L"-H", L"bogus"});
  auto r1 = bogus.run();

  EXPECT_EQ(r1.exit_code, 1);
  EXPECT_NE(r1.stderr_text.find("invalid archive format"),
            std::string::npos);

  Pipeline tar;
  tar.set_stdin("x\n");
  tar.add(L"cpio.exe", {L"-o", L"-H", L"tar"});
  auto r2 = tar.run();

  EXPECT_EQ(r2.exit_code, 1);
  EXPECT_NE(r2.stderr_text.find("not supported for --create"),
            std::string::npos);
}

// [#1462] Without -u, extraction does not replace an existing file that
// is newer or the same age as the archive member (GNU copyin rule);
// with -u it replaces unconditionally.
TEST(cpio, cpio_overwrite_age_rule_issue1462) {
  TempDir src;
  src.write("keep.txt", "fresh\n");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("keep.txt\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();
  EXPECT_EQ(created.exit_code, 0);

  TempDir dst;
  Pipeline extract1;
  extract1.set_cwd(dst.wpath());
  extract1.set_stdin(created.stdout_text);
  extract1.add(L"cpio.exe", {L"-i"});
  auto e1 = extract1.run();
  EXPECT_EQ(e1.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("keep.txt"), "fresh\n");

  // Local edit after extraction: its mtime is >= the member's.
  dst.write("keep.txt", "sentinel\n");

  Pipeline extract2;
  extract2.set_cwd(dst.wpath());
  extract2.set_stdin(created.stdout_text);
  extract2.add(L"cpio.exe", {L"-i"});
  auto e2 = extract2.run();
  EXPECT_EQ(e2.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("keep.txt"), "sentinel\n");
  EXPECT_NE(e2.stderr_text.find("not created: newer or same age"),
            std::string::npos);

  Pipeline extract3;
  extract3.set_cwd(dst.wpath());
  extract3.set_stdin(created.stdout_text);
  extract3.add(L"cpio.exe", {L"-i", L"-u"});
  auto e3 = extract3.run();
  EXPECT_EQ(e3.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("keep.txt"), "fresh\n");
}

// [#1462] -t -v uses GNU's long_format columns built from the header
// (mode string first), and --quiet suppresses the blocks report.
TEST(cpio, cpio_list_verbose_and_quiet_issue1462) {
  TempDir src;
  src.write("v.txt", "abc");

  Pipeline create;
  create.set_cwd(src.wpath());
  create.set_stdin("v.txt\n");
  create.add(L"cpio.exe", {L"-o", L"--quiet"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_TRUE(created.stderr_text.empty());

  Pipeline list;
  list.set_cwd(src.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t", L"-v"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_TRUE(listed.stdout_text.starts_with("-rw-r--r--"));
  EXPECT_NE(listed.stdout_text.find("v.txt"), std::string::npos);
}

// [#1462] A stream that does not start with a new-ASCII magic is an
// error, not a silent empty success.
TEST(cpio, cpio_bad_magic_is_an_error_issue1462) {
  Pipeline p;
  p.set_stdin("this is not an archive at all........");
  p.add(L"cpio.exe", {L"-i"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("unsupported archive format"),
            std::string::npos);
}
