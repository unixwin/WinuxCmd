/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(patch, patch_basic) {
  TempDir tmp;
  tmp.write("file.txt", "hello\n");
  std::string patch_data =
      "--- file.txt\n+++ file.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});

  TEST_LOG_CMD_LIST("patch.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("patch stdout", r.stdout_text);
  TEST_LOG("patch stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("file.txt"), "world\n");
}

TEST(patch, patch_multiple_hunks_use_cumulative_offsets) {
  TempDir tmp;
  tmp.write("file.txt", "a\nb\nc\nd\n");
  std::string patch_data =
      "--- file.txt\t2026-08-13 00:00:00\n"
      "+++ file.txt\t2026-08-13 00:00:01\n"
      "@@ -1,1 +1,2 @@\n"
      " a\n"
      "+x\n"
      "@@ -3,1 +4,1 @@\n"
      "-c\n"
      "+z\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch multi stdout", r.stdout_text);
  TEST_LOG("patch multi stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("file.txt"), "a\nx\nb\nz\nd\n");
}

TEST(patch, patch_directory_force_fuzz_and_quiet_options) {
  TempDir tmp;
  tmp.mkdir("src");
  tmp.write("src/file.txt", "hello\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- file.txt\n+++ file.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p.add(L"patch.exe",
        {L"--directory", L"src", L"--force", L"--fuzz=1", L"--quiet"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(tmp.read("src/file.txt"), "world\n");
}

// [GNU] Each `--- / +++ ` header pair starts a new file's hunk group and
// must be applied to its own target.
TEST(patch, patch_multi_file_patches_each_target) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n");
  tmp.write("b.txt", "bravo\n");
  std::string patch_data =
      "--- a.txt\n"
      "+++ a.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-alpha\n"
      "+ALPHA\n"
      "--- b.txt\n"
      "+++ b.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-bravo\n"
      "+BRAVO\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch multi-file stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("a.txt"), "ALPHA\n");
  EXPECT_EQ_TEXT(tmp.read("b.txt"), "BRAVO\n");
  EXPECT_TRUE(tmp.read("b.rej").empty());
}

// A blank line inside a hunk is context for BOTH sides.
TEST(patch, patch_blank_line_is_context_on_both_sides) {
  TempDir tmp;
  tmp.write("f.txt", "start\n\nend\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,3 +1,3 @@\n"
      " start\n"
      "\n"
      "-end\n"
      "+finish\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch blank stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "start\n\nfinish\n");
}

// "\ No newline at end of file" on the new side suppresses the trailing
// newline; on the old side it must match a file lacking one.
TEST(patch, patch_no_newline_at_eof_new_side) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-hello\n"
      "\\ No newline at end of file\n"
      "+world\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
}

TEST(patch, patch_no_newline_at_eof_old_side) {
  TempDir tmp;
  tmp.write("f.txt", std::string("hello"));  // no trailing newline
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-hello\n"
      "\\ No newline at end of file\n"
      "+world\n"
      "\\ No newline at end of file\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), std::string("world"));
}

// [GNU] Hunk found away from the hinted position reports the offset.
TEST(patch, patch_offset_reporting_wording) {
  TempDir tmp;
  tmp.write("f.txt", "x\nx\nx\nx\nhello\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-hello\n"
      "+world\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch offset stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "x\nx\nx\nx\nworld\n");
  EXPECT_TRUE(r.stdout_text.find("Hunk #1 succeeded at 5 (offset 4 lines).") !=
              std::string::npos);
}

// [GNU] A hunk that only matches with dropped context reports the fuzz.
TEST(patch, patch_fuzz_reporting_wording) {
  TempDir tmp;
  // The leading context line differs ("old" vs "CHANGED"), so the hunk can
  // only match after dropping it (fuzz 1).
  tmp.write("f.txt", "CHANGED\ntarget\ntail\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,3 +1,2 @@\n"
      " old\n"
      "-target\n"
      " tail\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch fuzz stdout", r.stdout_text);
  TEST_LOG("patch fuzz stderr", r.stderr_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "CHANGED\ntail\n");
  EXPECT_TRUE(r.stdout_text.find("Hunk #1 succeeded at 1 with fuzz 1") !=
              std::string::npos);
}

// [GNU] A failing hunk is rejected into <target>.rej by default.
TEST(patch, patch_reject_file_created_on_failure) {
  TempDir tmp;
  tmp.write("f.txt", "one\ntwo\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-nomatch\n"
      "+yes\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch rej stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "one\ntwo\n");
  std::string rej = tmp.read("f.txt.rej");
  TEST_LOG("reject content", rej);
  // [GNU] count 1 omits ",1" in reject headers.
  EXPECT_TRUE(rej.find("@@ -1 +1 @@") != std::string::npos);
  EXPECT_TRUE(rej.find("-nomatch") != std::string::npos);
  EXPECT_TRUE(rej.find("+yes") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 out of 1 hunk FAILED") !=
              std::string::npos);
}

// --dry-run must not write the target, rejects, or backups.
TEST(patch, patch_dry_run_writes_nothing) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");
  std::string failing_patch =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-nomatch\n"
      "+yes\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(failing_patch);
  p.add(L"patch.exe", {L"--dry-run", L"-b"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "hello\n");
  EXPECT_TRUE(tmp.read("f.txt.rej").empty());
  EXPECT_TRUE(tmp.read("f.txt.orig").empty());

  // Successful dry-run also leaves the file untouched.
  std::string good_patch =
      "--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n";
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin(good_patch);
  p2.add(L"patch.exe", {L"--dry-run"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "hello\n");
}

// [GNU] Default backup-if-mismatch in GNU mode: a clean apply makes no
// backup; fuzz/offset or failures do (even without -b).  -b always backs up.
TEST(patch, patch_backup_if_mismatch) {
  TempDir tmp;
  tmp.write("clean.txt", "hello\n");
  tmp.write("offset.txt", "x\nx\nhello\n");
  std::string clean_patch =
      "--- clean.txt\n+++ clean.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n";
  std::string offset_patch =
      "--- offset.txt\n+++ offset.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n";

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.set_stdin(clean_patch);
  p1.add(L"patch.exe", {});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_TRUE(tmp.read("clean.txt.orig").empty());
  EXPECT_EQ_TEXT(tmp.read("clean.txt"), "world\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin(offset_patch);
  p2.add(L"patch.exe", {});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("offset.txt.orig"), "x\nx\nhello\n");
  EXPECT_EQ_TEXT(tmp.read("offset.txt"), "x\nx\nworld\n");

  // -b backs up even on a clean apply.
  tmp.write("always.txt", "hello\n");
  Pipeline p3;
  p3.set_cwd(tmp.wpath());
  p3.set_stdin(
      "--- always.txt\n+++ always.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p3.add(L"patch.exe", {L"-b"});
  auto r3 = p3.run();
  EXPECT_EQ(r3.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("always.txt.orig"), "hello\n");
}

// Exit codes: 0 all applied, 1 some hunk failed, 2 serious trouble.
TEST(patch, patch_exit_codes) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");

  // 0: everything applied.
  Pipeline ok;
  ok.set_cwd(tmp.wpath());
  ok.set_stdin("--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  ok.add(L"patch.exe", {});
  EXPECT_EQ(ok.run().exit_code, 0);

  // 1: a hunk failed (even with --force).
  Pipeline fail;
  fail.set_cwd(tmp.wpath());
  fail.set_stdin("--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-nope\n+nope\n");
  fail.add(L"patch.exe", {L"--force"});
  auto rf = fail.run();
  EXPECT_EQ(rf.exit_code, 1);
  EXPECT_TRUE(!tmp.read("f.txt.rej").empty());

  // 2: usage error (negative fuzz).
  Pipeline usage;
  usage.set_cwd(tmp.wpath());
  usage.set_stdin("--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-world\n+hello\n");
  usage.add(L"patch.exe", {L"-F", L"-1"});
  auto ru = usage.run();
  EXPECT_EQ(ru.exit_code, 2);

  // 2: cannot open target file.
  Pipeline missing;
  missing.set_cwd(tmp.wpath());
  missing.set_stdin("--- gone.txt\n+++ gone.txt\n@@ -1,1 +1,1 @@\n-a\n+b\n");
  missing.add(L"patch.exe", {});
  EXPECT_EQ(missing.run().exit_code, 2);
}

// `--- /dev/null` patches create the +++ target file.
TEST(patch, patch_dev_null_creates_file) {
  TempDir tmp;
  std::string patch_data =
      "--- /dev/null\n"
      "+++ new.txt\n"
      "@@ -0,0 +1,2 @@\n"
      "+first\n"
      "+second\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  TEST_LOG("patch devnull stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("new.txt"), "first\nsecond\n");
}

// Patching an existing empty file must work.
TEST(patch, patch_empty_target_file) {
  TempDir tmp;
  tmp.write("f.txt", "");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -0,0 +1,1 @@\n"
      "+only\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "only\n");
}

// [GNU] -N/--forward skips hunks that are already applied.
TEST(patch, patch_forward_skips_already_applied) {
  TempDir tmp;
  tmp.write("f.txt", "world\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-hello\n"
      "+world\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {L"-N"});
  auto r = p.run();
  TEST_LOG("patch forward stdout", r.stdout_text);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
}

// [GNU] -R reverses old and new.
TEST(patch, patch_reverse_applies_backwards) {
  TempDir tmp;
  tmp.write("f.txt", "world\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-hello\n"
      "+world\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {L"-R"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "hello\n");
}

// The target file's CRLF line endings are preserved.
TEST(patch, patch_preserves_crlf_line_endings) {
  TempDir tmp;
  tmp.write("f.txt", "hello\r\nworld\r\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,2 +1,2 @@\n"
      " hello\n"
      "-world\n"
      "+planet\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "hello\r\nplanet\r\n");
}

// -F/--fuzz accepts large values; -i/--input reads the patch from a file.
TEST(patch, patch_input_option_and_fuzz_validation) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");
  tmp.write("p.diff",
            "--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"patch.exe", {L"--input", L"p.diff", L"--fuzz", L"4"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
}
