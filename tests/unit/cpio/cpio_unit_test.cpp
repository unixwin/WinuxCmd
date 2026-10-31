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
  EXPECT_NE(listed.stderr_text.find("cannot open archive"), std::string::npos);
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

// [refs #1127] --append splices new members before the trailer of an
// existing -F archive; without -F it is the oracle's usage error
// (cpio 2.15 main.c:616).
TEST(cpio, append_adds_members_to_existing_archive) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");
  tmp.write("b.txt", "BBB");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("a.txt\n");
  create.add(L"cpio.exe", {L"-o", L"-F", L"arc.cpio"});
  EXPECT_EQ(create.run().exit_code, 0);

  Pipeline append;
  append.set_cwd(tmp.wpath());
  append.set_stdin("b.txt\n");
  append.add(L"cpio.exe", {L"-o", L"-A", L"-F", L"arc.cpio"});
  auto appended = append.run();
  EXPECT_EQ(appended.exit_code, 0);

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.add(L"cpio.exe", {L"-t", L"-F", L"arc.cpio"});
  auto listed = list.run();
  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_NE(listed.stdout_text.find("a.txt"), std::string::npos);
  EXPECT_NE(listed.stdout_text.find("b.txt"), std::string::npos);
}

TEST(cpio, append_requires_archive_file) {
  Pipeline p;
  p.set_stdin("a.txt\n");
  p.add(L"cpio.exe", {L"-o", L"--append"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("no archive file name is given"),
            std::string::npos);
}

// [refs #1127] --block-size pads the archive to whole 512-byte blocks;
// a bad value is the oracle's "invalid block size" error.
TEST(cpio, block_size_pads_archive_and_validates) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("a.txt\n");
  create.add(L"cpio.exe", {L"-o", L"-F", L"arc.cpio", L"--block-size", L"10"});
  EXPECT_EQ(create.run().exit_code, 0);
  EXPECT_EQ(tmp.read("arc.cpio").size() % 5120, 0u);

  Pipeline bad;
  bad.set_cwd(tmp.wpath());
  bad.set_stdin("a.txt\n");
  bad.add(L"cpio.exe", {L"-o", L"--block-size", L"0"});
  auto r_bad = bad.run();
  EXPECT_EQ(r_bad.exit_code, 1);
  EXPECT_NE(r_bad.stderr_text.find("invalid block size"), std::string::npos);
}

// [refs #1127] --device-independent renumbers inodes from 1, making
// repeated archives of the same input byte-identical.
TEST(cpio, device_independent_renumbers_inodes) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");
  tmp.write("b.txt", "BBB");

  Pipeline first;
  first.set_cwd(tmp.wpath());
  first.set_stdin("a.txt\nb.txt\n");
  first.add(L"cpio.exe", {L"-o", L"--device-independent"});
  auto r1 = first.run();
  EXPECT_EQ(r1.exit_code, 0);
  // newc header: magic (6) then the 8-hex-digit inode field.
  EXPECT_TRUE(r1.stdout_text.substr(6, 8) == "00000001");

  Pipeline second;
  second.set_cwd(tmp.wpath());
  second.set_stdin("a.txt\nb.txt\n");
  second.add(L"cpio.exe", {L"-o", L"--reproducible"});
  auto r2 = second.run();
  EXPECT_EQ(r2.stdout_text, r1.stdout_text);
}

// [refs #1127] -D/--directory changes directory before members are
// processed; with -d a missing target directory is created.
TEST(cpio, directory_option_changes_member_context) {
  TempDir tmp;
  tmp.write("src/f.txt", "from-src");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("f.txt\n");
  create.add(L"cpio.exe", {L"-o", L"-D", L"src"});
  auto created = create.run();
  EXPECT_EQ(created.exit_code, 0);
  EXPECT_NE(created.stdout_text.find("f.txt"), std::string::npos);

  TempDir dst;
  Pipeline extract;
  extract.set_cwd(dst.wpath());
  extract.set_stdin(created.stdout_text);
  extract.add(L"cpio.exe", {L"-i", L"-d", L"-D", L"out"});
  auto extracted = extract.run();
  EXPECT_EQ(extracted.exit_code, 0);
  EXPECT_EQ_TEXT(dst.read("out/f.txt"), "from-src");
}

// [refs #1127] -V/--dot prints one dot per member to standard error.
TEST(cpio, dot_prints_one_dot_per_member) {
  TempDir tmp;
  tmp.write("a.txt", "AAA");
  tmp.write("b.txt", "BBB");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("a.txt\nb.txt\n");
  create.add(L"cpio.exe", {L"-o", L"--dot"});
  auto r = create.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text, "..");
}

// [refs #1127] Copy-pass mode (-p) is implemented: listed files are
// copied into the destination directory.
TEST(cpio, pass_through_copies_into_destination) {
  TempDir tmp;
  tmp.write("a.txt", "pass-me");
  tmp.write("sub/b.txt", "nested");

  Pipeline pass;
  pass.set_cwd(tmp.wpath());
  pass.set_stdin("a.txt\nsub/b.txt\n");
  pass.add(L"cpio.exe", {L"-p", L"-d", L"dest"});
  auto r = pass.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("dest/a.txt"), "pass-me");
  EXPECT_EQ_TEXT(tmp.read("dest/sub/b.txt"), "nested");
}

TEST(cpio, pass_through_requires_destination) {
  Pipeline p;
  p.set_stdin("a.txt\n");
  p.add(L"cpio.exe", {L"-p"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("destination directory"), std::string::npos);
}

// [refs #1127] A genuinely empty file is archived (it used to be
// skipped as if unreadable) and listed with size 0.
TEST(cpio, empty_file_is_archived) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("empty.txt\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();
  EXPECT_EQ(created.exit_code, 0);

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t"});
  auto listed = list.run();
  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_EQ_TEXT(listed.stdout_text, "-          0 empty.txt\n");
}
