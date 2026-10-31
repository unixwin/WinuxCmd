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

  // 1: the target file does not exist — GNU patch runs the
  // file-name dialogue; the patch is on stdin, so the prompts read
  // EOF and the file is skipped as ignored (pch.c:270-320).
  Pipeline missing;
  missing.set_cwd(tmp.wpath());
  missing.set_stdin("--- gone.txt\n+++ gone.txt\n@@ -1,1 +1,1 @@\n-a\n+b\n");
  missing.add(L"patch.exe", {});
  auto rm = missing.run();
  EXPECT_EQ(rm.exit_code, 1);
  EXPECT_TRUE(rm.stdout_text.find("can't find file to patch at input "
                                  "line 3") != std::string::npos);
  EXPECT_TRUE(rm.stdout_text.find("1 out of 1 hunk ignored") !=
              std::string::npos);
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
// [GNU] -N on an already-applied patch: the first-hunk DWIM detects
// the reversed patch and skips the whole file; the hunks count as
// ignored (exit 1) and are rejected (patch.c:424-458, util.c:1043).
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
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
  EXPECT_TRUE(r.stdout_text.find("Reversed (or previously applied) patch "
                                 "detected!  Skipping patch.") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 out of 1 hunk ignored -- saving rejects "
                                 "to file f.txt.rej") != std::string::npos);
  EXPECT_TRUE(tmp.read("f.txt.rej").find("-hello") != std::string::npos);
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

// [#1212] a 0x1A in the patch stream truncated the hunk.
TEST(patch, patch_stdin_ctrlz_not_truncated_issue1212) {
  TempDir tmp;
  tmp.write("p.txt",
            "ctx\x1a"
            "AAA\nBBB\n");
  const std::string patch =
      "--- p.txt\n+++ p.txt\n@@ -1,2 +1,2 @@\n ctx\x1a"
      "AAA\n-BBB\n+XXX\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch);
  p.add(L"patch.exe", {L"p.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("p.txt"), std::string("ctx\x1a"
                                           "AAA\nXXX\n",
                                           12));
}

// [GNU] Issue #1381: -l matches lines that differ only in whitespace-run
// length; a wholly missing space is still a mismatch.
TEST(patch, issue_1381_ignore_whitespace_runs) {
  TempDir tmp;
  tmp.write("v.txt", "a  b\nc d\n");
  std::string patch_data =
      "--- v.txt\n+++ v.txt\n@@ -1,2 +1,2 @@\n a b\n-c d\n+C D\n";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {L"-l", L"v.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("v.txt"), "a  b\nC D\n");

  // Without -l the whitespace-mismatched context line only matches by
  // fuzz (GNU drops the unmatched prefix context); with -l it applies
  // cleanly at the stated position.
  tmp.write("v.txt", "a  b\nc d\n");
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin(patch_data);
  p2.add(L"patch.exe", {L"v.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("with fuzz") != std::string::npos);
}

// [GNU] Issue #1381: forcing a non-unified format on unified input is
// "Only garbage" (rc 2); --unified/-u apply it.
TEST(patch, issue_1381_format_forcing) {
  TempDir tmp;
  tmp.write("v.txt", "old\n");
  std::string patch_data =
      "--- v.txt\n+++ v.txt\n@@ -1,1 +1,1 @@\n-old\n+new\n";

  for (const wchar_t* opt : {L"--context", L"--normal", L"--ed", L"-c",
                             L"-n", L"-e"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin(patch_data);
    p.add(L"patch.exe", {opt, L"v.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_EQ_TEXT(tmp.read("v.txt"), "old\n");
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {L"--unified", L"v.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("v.txt"), "new\n");
}

// [GNU] Issue #1381: --version-control naming, including the measured
// 2.7.6 quirk that "none"/"off" selects numbered backups.
TEST(patch, issue_1381_version_control) {
  TempDir tmp;
  std::string bad_patch =
      "--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-WRONG\n+new\n";
  auto run_vc = [&](const std::wstring& arg) {
    tmp.write("f.txt", "old\n");
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin(bad_patch);
    p.add(L"patch.exe", {arg, L"f.txt"});
    return p.run().exit_code;
  };

  EXPECT_EQ(run_vc(L"--version-control=simple"), 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "f.txt.orig"));
  std::filesystem::remove(tmp.path / "f.txt.orig");

  EXPECT_EQ(run_vc(L"--version-control=numbered"), 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "f.txt.~1~"));
  std::filesystem::remove(tmp.path / "f.txt.~1~");

  EXPECT_EQ(run_vc(L"--version-control=none"), 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "f.txt.~1~"));
  std::filesystem::remove(tmp.path / "f.txt.~1~");

  EXPECT_EQ(run_vc(L"--version-control=bogus"), 2);
}

// [GNU] Issue #1381: --get validates its value; --quoting-style=c quotes
// the file name in the "patching file" line.
TEST(patch, issue_1381_get_and_quoting_style) {
  TempDir tmp;
  tmp.write("f.txt", "old\n");
  std::string good_patch =
      "--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-old\n+new\n";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(good_patch);
  p.add(L"patch.exe", {L"--get=abc", L"f.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin(good_patch);
  p2.add(L"patch.exe", {L"--quoting-style=c", L"f.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_TRUE(r2.stdout_text.find("patching file \"f.txt\"") !=
              std::string::npos);
}

// #1374: patch expands its ORIGFILE and PATCHFILE operands in
// WINUX_SHELL_GLOB=native mode.
TEST(patch, patch_native_glob_expands_operands_issue1374) {
  TempDir tmp;
  tmp.write("porig.dat", "old\n");
  tmp.write("change.patch",
            "--- porig.dat\n+++ porig.dat\n@@ -1 +1 @@\n-old\n+new\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUX_SHELL_GLOB", L"native");
  p.add(L"patch.exe", {L"porig.da*", L"change.pa*"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream in(tmp.path / "porig.dat", std::ios::binary);
  std::string applied((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ_TEXT(applied, "new\n");
}

// [GNU] #1345: -p/--strip must actually strip leading components from
// the header file names.  The option was decoded with the wrong value
// type, so the count silently stayed 0 and `patch -p1` tried to open
// the literal 'b/src/f.txt'.
TEST(patch, patch_strip_applies_to_header_names_issue1345) {
  const std::string diff =
      "--- a/src/f.txt\n"
      "+++ b/src/f.txt\n"
      "@@ -1,3 +1,3 @@\n"
      " l1\n"
      "-l2\n"
      "+L2\n"
      " l3\n";

  // -p1 applies to src/f.txt.
  {
    TempDir tmp;
    tmp.mkdir("src");
    tmp.write("src/f.txt", "l1\nl2\nl3\n");
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin(diff);
    p.add(L"patch.exe", {L"-p1"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(tmp.read("src/f.txt"), "l1\nL2\nl3\n");
  }
  // --strip=1 and --strip 1 behave identically.
  for (const auto& args :
       {std::vector<std::wstring>{L"--strip=1"},
        std::vector<std::wstring>{L"--strip", L"1"}}) {
    TempDir tmp;
    tmp.mkdir("src");
    tmp.write("src/f.txt", "l1\nl2\nl3\n");
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin(diff);
    p.add(L"patch.exe", args);
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(tmp.read("src/f.txt"), "l1\nL2\nl3\n");
  }
  // -p0 keeps the full header path, which does not exist here: GNU
  // patch runs the file-name dialogue; with the patch itself on
  // stdin the prompts read EOF and the file is skipped as ignored
  // (exit 1), leaving everything untouched (pch.c:270-320).
  {
    TempDir tmp;
    tmp.mkdir("src");
    tmp.write("src/f.txt", "l1\nl2\nl3\n");
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_stdin(diff);
    p.add(L"patch.exe", {L"-p0"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_EQ_TEXT(tmp.read("src/f.txt"), "l1\nl2\nl3\n");
  }
}

// [GNU] #1345: a negative strip count is a usage error (exit 2), and
// so is an unrecognized option.
TEST(patch, patch_usage_errors_exit_2_issue1345) {
  TempDir tmp;
  tmp.write("f.txt", "l1\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("");
  p.add(L"patch.exe", {L"-p-1"});
  EXPECT_EQ(p.run().exit_code, 2);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin("");
  p2.add(L"patch.exe", {L"--bogus"});
  EXPECT_EQ(p2.run().exit_code, 2);
}

// [GNU] #1346: a context diff is parsed and applied (previously it
// printed "patching file" and exited 0 without changing anything).
TEST(patch, patch_context_diff_applies_issue1346) {
  TempDir tmp;
  tmp.write("f.txt", "l1\nl2\nl3\n");
  const std::string diff =
      "*** f.txt\t2026-01-01 00:00:00\n"
      "--- f.txt\t2026-01-01 00:00:01\n"
      "***************\n"
      "*** 1,3 ****\n"
      "  l1\n"
      "! l2\n"
      "  l3\n"
      "--- 1,3 ----\n"
      "  l1\n"
      "! L2\n"
      "  l3\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(diff);
  p.add(L"patch.exe", {L"f.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("patching file f.txt") != std::string::npos);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "l1\nL2\nl3\n");
}

// [GNU] #1346: in a pure-addition context hunk, GNU diff omits the old
// section's lines entirely (only its range header is printed); the
// hunk must still apply at the right place.
TEST(patch, patch_context_diff_pure_addition_issue1346) {
  TempDir tmp;
  tmp.write("f.txt", "a\nb\nc\nd\ne\n");
  const std::string diff =
      "*** f.txt\t2026-01-01 00:00:00\n"
      "--- f.txt\t2026-01-01 00:00:01\n"
      "***************\n"
      "*** 1,5 ****\n"
      "--- 1,6 ----\n"
      "  a\n"
      "  b\n"
      "+ X\n"
      "  c\n"
      "  d\n"
      "  e\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(diff);
  p.add(L"patch.exe", {L"f.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "a\nb\nX\nc\nd\ne\n");
}

// [GNU] #1346: a normal (ed-style) diff is parsed and applied at the
// stated positions, including a later command whose position shifts
// because of an earlier one.
TEST(patch, patch_normal_diff_applies_issue1346) {
  TempDir tmp;
  tmp.write("f.txt", "l1\nl2\nl3\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("1c1\n< l1\n---\n> L1\n");
  p.add(L"patch.exe", {L"f.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "L1\nl2\nl3\n");

  TempDir tmp2;
  tmp2.write("f.txt", "x\nY\nz\n");
  Pipeline p2;
  p2.set_cwd(tmp2.wpath());
  p2.set_stdin("2c2\n< Y\n---\n> y\n3a4\n> w\n");
  p2.add(L"patch.exe", {L"f.txt"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(tmp2.read("f.txt"), "x\ny\nz\nw\n");

  // Content that does not match the stated position fails the hunk.
  TempDir tmp3;
  tmp3.write("f.txt", "XX\nl2\nl3\n");
  Pipeline p3;
  p3.set_cwd(tmp3.wpath());
  p3.set_stdin("1c1\n< l1\n---\n> L1\n");
  p3.add(L"patch.exe", {L"f.txt"});
  auto r3 = p3.run();
  EXPECT_EQ(r3.exit_code, 1);
  EXPECT_EQ_TEXT(tmp3.read("f.txt"), "XX\nl2\nl3\n");
}

// [GNU] #1346: input that names files but contains no hunks is
// garbage (exit 2), for both unified and context headers — it must
// never be reported as a successful patch.
TEST(patch, patch_header_without_hunks_is_garbage_issue1346) {
  TempDir tmp;
  tmp.write("f.txt", "l1\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- f.txt\n+++ f.txt\n");
  p.add(L"patch.exe", {});
  EXPECT_EQ(p.run().exit_code, 2);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "l1\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.set_stdin("*** f.txt\n--- f.txt\n");
  p2.add(L"patch.exe", {L"f.txt"});
  EXPECT_EQ(p2.run().exit_code, 2);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "l1\n");
}

namespace patch_dialect_detail {
// "C:\Users\...\wct123" -> "/c/Users/.../wct123" (MSYS drive spelling:
// the colon is dropped, only the letter stays) — same fold the shred
// #1240 tests use.
inline auto slash_drive_form(const std::filesystem::path &path)
    -> std::wstring {
  std::wstring text = path.wstring();
  if (text.size() >= 2 && text[0] >= L'A' && text[0] <= L'Z') {
    text[0] = static_cast<wchar_t>(text[0] - L'A' + L'a');
  }
  text.erase(1, 1);  // drop the ':' from "c:\..."
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}
}  // namespace patch_dialect_detail

// [GNU] rootaudit #1364: the patch file operand must be resolved
// through the shared path boundary.  A bare "/"-leading spelling
// handed raw to CreateFileW lands on the process current drive root
// and the file is not found; through the boundary it names the file.
TEST(patch, patch_input_operand_uses_shared_path_boundary_rootaudit1364) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");
  tmp.write("changes.diff",
            "--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  const std::wstring patch_operand =
      patch_dialect_detail::slash_drive_form(tmp.path / L"changes.diff");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"patch.exe", {L"-i", patch_operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
}

// [GNU] Reject files open with the patch's own header pair, carry the
// hunk's function context, and use output coordinates: a reject after
// a line-adding hunk is shifted by that hunk's delta
// (abort_hunk_unified, patch.c:1273).
TEST(patch, patch_reject_header_coordinates_and_context) {
  TempDir tmp;
  std::string original;
  for (int i = 1; i <= 20; ++i) original += std::to_string(i) + "\n";
  tmp.write("orig.txt", original);
  std::string patch_data =
      "--- orig.txt\n"
      "+++ orig.txt\n"
      "@@ -2,2 +2,4 @@\n"
      " 2\n"
      "+2a\n"
      "+2b\n"
      " 3\n"
      "@@ -18,3 +18,3 @@ somefunc\n"
      " 18\n"
      "-19\n"
      "+nineteen\n"
      " 20\n";
  // Line 19 already differs, so hunk 2 fails after hunk 1 added 2.
  std::string work = original;
  work.replace(work.find("\n19\n"), 4, "\nNINETEEN\n");
  tmp.write("work.txt", work);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {L"work.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("Hunk #2 FAILED at 20.") !=
              std::string::npos);
  const std::string rej = tmp.read("work.txt.rej");
  EXPECT_TRUE(rej.find("--- orig.txt\n+++ orig.txt\n") != std::string::npos);
  EXPECT_TRUE(rej.find("@@ -20,3 +20,3 @@ somefunc") != std::string::npos);
  EXPECT_TRUE(rej.find("-19") != std::string::npos);
}

// [GNU] Under --dry-run the banner is "checking file", failures still
// print the summary (without the saving clause), and no reject file
// appears (patch.c:632-670).
TEST(patch, patch_dry_run_reports_summary_without_saving) {
  TempDir tmp;
  tmp.write("f.txt", "one\ntwo\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-nomatch\n+yes\n");
  p.add(L"patch.exe", {L"--dry-run"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("checking file f.txt") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 out of 1 hunk FAILED") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("saving rejects") == std::string::npos);
  EXPECT_TRUE(tmp.read("f.txt.rej").empty());
}

// [GNU] The offset in a success line is the cumulative in_offset, and
// it keeps printing on later hunks whose own match is exact once the
// standing offset is nonzero (patch.c:487-499).
TEST(patch, patch_success_offset_is_cumulative) {
  TempDir tmp;
  tmp.write("f.txt", "pad\npad\na\nb\nc\n");
  std::string patch_data =
      "--- f.txt\n"
      "+++ f.txt\n"
      "@@ -1,1 +1,1 @@\n"
      "-a\n"
      "+A\n"
      "@@ -2,1 +2,1 @@\n"
      "-b\n"
      "+B\n";
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Hunk #1 succeeded at 3 (offset 2 "
                                 "lines).") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Hunk #2 succeeded at 4 (offset 2 "
                                 "lines).") != std::string::npos);
}

// [GNU] fatal() wording (util.c) and numeric_string diagnostics
// (patch.c:1094): garbage input and bad -F/-p values.
TEST(patch, patch_garbage_and_numeric_diagnostics) {
  TempDir tmp;
  tmp.write("f.txt", "hello\n");

  Pipeline garbage;
  garbage.set_cwd(tmp.wpath());
  garbage.set_stdin("this is not a patch\n");
  garbage.add(L"patch.exe", {});
  auto rg = garbage.run();
  EXPECT_EQ(rg.exit_code, 2);
  EXPECT_TRUE(rg.stderr_text.find(
                  "patch: **** Only garbage was found in the patch "
                  "input.") != std::string::npos);

  // An empty patch input is a silent success.
  Pipeline empty;
  empty.set_cwd(tmp.wpath());
  empty.set_stdin("");
  empty.add(L"patch.exe", {});
  auto re = empty.run();
  EXPECT_EQ(re.exit_code, 0);
  EXPECT_TRUE(re.stdout_text.empty());
  EXPECT_TRUE(re.stderr_text.empty());

  Pipeline fuzz;
  fuzz.set_cwd(tmp.wpath());
  fuzz.set_stdin("");
  fuzz.add(L"patch.exe", {L"-F", L"abc"});
  auto rf = fuzz.run();
  EXPECT_EQ(rf.exit_code, 2);
  EXPECT_TRUE(rf.stderr_text.find("patch: **** fuzz factor abc is not a "
                                  "number") != std::string::npos);

  Pipeline strip;
  strip.set_cwd(tmp.wpath());
  strip.set_stdin("");
  strip.add(L"patch.exe", {L"--strip=-1"});
  auto rs = strip.run();
  EXPECT_EQ(rs.exit_code, 2);
  EXPECT_TRUE(rs.stderr_text.find("patch: **** strip count -1 is "
                                  "negative") != std::string::npos);
}

// [GNU] -r - discards rejects: the summary loses its saving clause
// and no file named "-" is created (patch.c:640).
TEST(patch, patch_reject_file_dash_discards) {
  TempDir tmp;
  tmp.write("f.txt", "one\ntwo\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- f.txt\n+++ f.txt\n@@ -1,1 +1,1 @@\n-nomatch\n+yes\n");
  p.add(L"patch.exe", {L"-r", L"-"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("1 out of 1 hunk FAILED") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("saving rejects") == std::string::npos);
  EXPECT_TRUE(tmp.read("-").empty());
}

// [GNU] --batch with a missing target: no prompts, the file counts as
// ignored (pch.c:288).
TEST(patch, patch_batch_missing_target_skips) {
  TempDir tmp;
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- ghost.txt\n+++ ghost.txt\n@@ -1,1 +1,1 @@\n-a\n+b\n");
  p.add(L"patch.exe", {L"--batch"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("No file to patch.  Skipping patch.") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("1 out of 1 hunk ignored") !=
              std::string::npos);
}

// [GNU] Default name preparation is basename (util.c fetchname with
// strippath -1): when both a/b/f.txt and f.txt exist, the patch goes
// to f.txt, not to the path-ful name.
TEST(patch, patch_default_names_use_basename) {
  TempDir tmp;
  tmp.mkdir("a");
  tmp.write("a/f.txt", "decoy\n");
  tmp.write("f.txt", "hello\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- a/f.txt\n+++ a/f.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p.add(L"patch.exe", {});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("f.txt"), "world\n");
  EXPECT_EQ_TEXT(tmp.read("a/f.txt"), "decoy\n");
}

// [GNU] best_name (pch.c:1039): with -p0 and several existing
// candidates, fewest path components wins over old/new order.
TEST(patch, patch_best_name_prefers_fewest_components) {
  TempDir tmp;
  tmp.mkdir("dir");
  tmp.write("dir/longname.txt", "other\n");
  tmp.write("s.txt", "hello\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- dir/longname.txt\n+++ s.txt\n"
              "@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p.add(L"patch.exe", {L"-p0"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("s.txt"), "world\n");
  EXPECT_EQ_TEXT(tmp.read("dir/longname.txt"), "other\n");
}

// [GNU] -pN ignores a name with fewer than N slash-runs entirely
// (util.c fetchname): here only the new name survives -p1.
TEST(patch, patch_strip_ignores_name_with_too_few_slashes) {
  TempDir tmp;
  tmp.write("x.txt", "decoy\n");
  tmp.write("y.txt", "hello\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("--- x.txt\n+++ a/y.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p.add(L"patch.exe", {L"-p1"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("y.txt"), "world\n");
  EXPECT_EQ_TEXT(tmp.read("x.txt"), "decoy\n");
}

// [GNU] The Index name is discarded when a header name exists
// (pch.c:870, non-POSIX): a missing header file goes to the dialogue
// even though the Index file exists.  POSIX mode keeps it.
TEST(patch, patch_index_name_discarded_beside_header_names) {
  TempDir tmp;
  tmp.write("decoy.txt", "hello\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("Index: decoy.txt\n"
              "--- missing.txt\n+++ missing.txt\n"
              "@@ -1,1 +1,1 @@\n-hello\n+world\n");
  p.add(L"patch.exe", {L"--batch"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(tmp.read("decoy.txt"), "hello\n");

  Pipeline q;
  q.set_cwd(tmp.wpath());
  q.set_stdin("Index: decoy.txt\n"
              "--- missing.txt\n+++ missing.txt\n"
              "@@ -1,1 +1,1 @@\n-hello\n+world\n");
  q.add(L"patch.exe", {L"--batch", L"--posix"});
  auto r2 = q.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("decoy.txt"), "world\n");
}
