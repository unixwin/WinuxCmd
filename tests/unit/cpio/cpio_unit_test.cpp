/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(cpio, cpio_basic) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"cpio.exe", {L"-o"});

  TEST_LOG_CMD_LIST("cpio.exe", L"-o");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
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
  EXPECT_TRUE(created.stderr_text.empty());
  EXPECT_TRUE(created.stdout_text.starts_with("070701"));
  EXPECT_NE(created.stdout_text.find("cat-no-newline.txt"), std::string::npos);
  EXPECT_NE(created.stdout_text.find("TRAILER!!!"), std::string::npos);

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_EQ_TEXT(listed.stdout_text, "-          4 cat-no-newline.txt\n");
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
  EXPECT_EQ_TEXT(listed.stdout_text, "-          5 f.txt\n");
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
  EXPECT_TRUE(created.stderr_text.empty());

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
  EXPECT_TRUE(created.stderr_text.empty());

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i"});
  auto extracted = extract.run();

  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("NUL.txt"), "real-content\n");
}
