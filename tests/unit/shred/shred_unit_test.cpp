/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(shred, shred_basic) {
  TempDir tmp;
  tmp.write("secret.txt", "sensitive data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-u", L"secret.txt"});

  TEST_LOG_CMD_LIST("shred.exe", L"-u", L"secret.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_overwrite_count) {
  TempDir tmp;
  tmp.write("data.txt", "some data to overwrite");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"3", L"-u", L"data.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // File should be deleted after 3 overwrites
}

TEST(shred, shred_verbose) {
  TempDir tmp;
  tmp.write("verbose.txt", "verbose test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-v", L"-n", L"1", L"-u", L"verbose.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Verbose mode should show progress
}

TEST(shred, shred_zero_final) {
  TempDir tmp;
  tmp.write("zero.txt", "zero final test");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-z", L"-n", L"1", L"-u", L"zero.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_exact_mode) {
  TempDir tmp;
  tmp.write("exact.txt", "exact mode test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"--exact", L"-n", L"1", L"-u", L"exact.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --exact should not round up to block boundaries
}

TEST(shred, shred_remove) {
  TempDir tmp;
  tmp.write("remove.txt", "remove test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"--remove", L"-n", L"1", L"remove.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_force) {
  TempDir tmp;
  tmp.write("force.txt", "force test data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-f", L"-n", L"1", L"-u", L"force.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(shred, shred_nonexistent_file) {
  Pipeline p;
  p.add(L"shred.exe", {L"-u", L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}

TEST(shred, shred_zero_size_does_not_modify_file) {
  TempDir tmp;
  tmp.write("data.txt", "must remain intact");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-s", L"0", L"-n", L"1", L"data.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("data.txt"), "must remain intact");
}

// ===== #1240 regression: POSIX drive-dialect operands =====
//
// `shred /e/f` from a C: cwd handed the raw "/e/f" text to CreateFileW.
// Win32 resolves a leading "/e" against the CURRENT drive, so the command
// silently overwrote "C:\e\f" whenever that shadow existed (rc 0, wrong
// target destroyed) and reported "cannot open" for a perfectly good E:\
// file otherwise. Every operand must fold through the shared
// make_api_path_operand boundary before a shred lands. The probes below
// synthesize the dialect spelling from the TempDir's own drive, so the
// assertions work on any host regardless of which drives exist. Overwrite
// probes use -n 0 (zero passes: the open still proves where the operand
// resolved, content stays intact) wherever the landing is proven at the
// parse layer; the destructive proof runs exactly once on a file the test
// created itself.
namespace shred_dialect_detail {

// Lowercase drive letter of an absolute path ("C:\x" -> L'c').
auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

// UTF-8 form of a wide operand for exact stderr substring comparisons.
auto utf8(const std::wstring &text) -> std::string {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

// "C:\Users\...\wct123" -> "/c/Users/.../wct123" (MSYS drive spelling: the
// colon is dropped, only the letter stays).
auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..."
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

// "/c/...", "/cygdrive/c/..." and "/mnt/c/..." name the same file; the
// three spellings must all resolve through the shared fold (#1240).
auto prefixed_drive_form(const std::wstring &prefix,
                         const std::filesystem::path &path) -> std::wstring {
  return prefix + slash_drive_form(path);  // the MSYS form keeps its "/"
}

// First fixed drive whose letter differs from OTHER, or L'\0' when none.
auto other_fixed_drive(wchar_t other) -> wchar_t {
  wchar_t buf[512];
  const DWORD len =
      GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buf)), buf);
  for (DWORD i = 0; i + 1 < len;) {
    const wchar_t *entry = buf + i;
    const size_t entry_len = std::wcslen(entry);
    if (entry_len == 0) break;
    i += static_cast<DWORD>(entry_len) + 1;
    if (entry_len < 3 || entry[1] != L':' || entry[2] != L'\\') continue;
    wchar_t drive = entry[0];
    if (drive >= L'A' && drive <= L'Z') {
      drive = static_cast<wchar_t>(drive - L'A' + L'a');
    }
    if (drive == other) continue;
    if (GetDriveTypeW(entry) != DRIVE_FIXED) continue;
    return drive;
  }
  return L'\0';
}

// Where the BUGGY code put the probe leaf instead: it rooted the operand's
// first component ("<drive>") on the cwd's drive, so from cwd "X:" the
// probe appeared at "X:\<operand-drive>\<tmp-rest>\<leaf>". The leaf name
// is unique per TempDir, so the negative assertion never blames
// pre-existing machine state. This is the "shadow path is never shred"
// witness: asserted with an exists() probe, never with a real shred.
auto shadow_path(const TempDir &tmp, wchar_t cwd_drive,
                 const std::wstring &leaf) -> std::wstring {
  const wchar_t operand_drive = drive_letter(tmp.path);
  std::wstring full(1, cwd_drive);
  full += L":\\";
  full += operand_drive;
  full += L"\\";
  full += tmp.path.wstring().substr(3);  // past "X:\"
  full += L"\\";
  full += leaf;
  return full;
}

}  // namespace shred_dialect_detail

TEST(shred, shred_drive_dialect_zero_passes_open_operand_drive_file) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  const std::wstring operand =
      shred_dialect_detail::slash_drive_form(tmp.path / L"v1240.txt");
  const std::wstring shadow = shred_dialect_detail::shadow_path(
      tmp, shred_dialect_detail::drive_letter(tmp.path), L"v1240.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  // Parse-layer landing: the open hit the operand-drive file, content
  // untouched by the zero passes.
  EXPECT_EQ(tmp.read("v1240.txt"), "MARKER-1240");
  // Shadow negative (#1240): the unfolded operand rooted the leaf on the
  // cwd drive - that path must never exist, let alone be shredded.
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

TEST(shred, shred_cygdrive_and_mnt_spellings_open_same_file) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  const wchar_t drive = shred_dialect_detail::drive_letter(tmp.path);
  const std::wstring shadow =
      shred_dialect_detail::shadow_path(tmp, drive, L"v1240.txt");

  for (const auto *prefix : {L"/cygdrive", L"/mnt"}) {
    const std::wstring operand = shred_dialect_detail::prefixed_drive_form(
        prefix, tmp.path / L"v1240.txt");

    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"shred.exe", {L"-n", L"0", operand});

    auto r = p.run();

    TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("shred stderr", r.stderr_text);

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.empty());
    EXPECT_EQ(tmp.read("v1240.txt"), "MARKER-1240");
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
  }
}

TEST(shred, shred_drive_dialect_overwrite_lands_on_operand_drive) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  const std::wstring operand =
      shred_dialect_detail::slash_drive_form(tmp.path / L"v1240.txt");
  const std::wstring shadow = shred_dialect_detail::shadow_path(
      tmp, shred_dialect_detail::drive_letter(tmp.path), L"v1240.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"1", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"1", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  // The one destructive proof: the overwrite hit the file the test wrote,
  // which still exists but no longer holds the marker.
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"v1240.txt"));
  EXPECT_NE(tmp.read("v1240.txt"), "MARKER-1240");
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

TEST(shred, shred_drive_dialect_cwd_on_other_drive_lands_on_operand_drive) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  const wchar_t other = shred_dialect_detail::other_fixed_drive(
      shred_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      shred_dialect_detail::slash_drive_form(tmp.path / L"v1240.txt");
  // The audit's exact shadow shape: cwd on one drive, operand on another -
  // the unfolded spelling put the file at "<cwd>:\e\<tmp>\...".
  const std::wstring shadow =
      shred_dialect_detail::shadow_path(tmp, other, L"v1240.txt");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"shred.exe", {L"-n", L"0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("v1240.txt"), "MARKER-1240");
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

TEST(shred, shred_windows_drive_form_still_opens) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  // Native spellings must stay untouched by the fold (idempotence guard).
  const std::wstring operand = tmp.wpath() + L"\\v1240.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("v1240.txt"), "MARKER-1240");
}

TEST(shred, shred_missing_dialect_operand_fails_with_typed_spelling) {
  TempDir tmp;
  const std::wstring operand =
      shred_dialect_detail::slash_drive_form(tmp.path / L"absent1240.txt");
  const std::wstring shadow = shred_dialect_detail::shadow_path(
      tmp, shred_dialect_detail::drive_letter(tmp.path), L"absent1240.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  // False-success guard (#1240): a missing dialect operand must fail, and
  // the message must quote the operand spelling as typed - never a path
  // rooted on the cwd drive.
  EXPECT_NE(r.exit_code, 0);
  EXPECT_NE(r.stderr_text.find(shred_dialect_detail::utf8(operand)),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

TEST(shred, shred_bare_drive_root_dialect_fails_without_creating_shadow) {
  TempDir tmp;
  const wchar_t drive = shred_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;
  // What the buggy code would have opened here: "<drive>:\<drive>" - a
  // directory at the drive root. Only assert the negative when it does not
  // pre-exist.
  const std::wstring shadow_root = std::wstring(1, drive) + L":\\" + drive;
  const bool shadow_pre_existed =
      std::filesystem::exists(std::filesystem::path(shadow_root));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"0", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  // A drive root is a directory: shred must fail on it (opening a
  // directory for writing fails), and it must never have created or
  // opened anything at "<drive>:\<drive>".
  EXPECT_NE(r.exit_code, 0);
  if (!shadow_pre_existed) {
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow_root)));
  }
}

TEST(shred, shred_wildcard_dialect_pattern_expands_on_operand_drive) {
  TempDir tmp;
  tmp.write("g1240a.txt", "MARKER-1240");
  const std::wstring pattern = shred_dialect_detail::slash_drive_form(tmp.path /
                                                                      L"g1240"
                                                                      L"*.txt");
  const std::wstring shadow = shred_dialect_detail::shadow_path(
      tmp, shred_dialect_detail::drive_letter(tmp.path), L"g1240a.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"0", pattern});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-n", L"0", pattern);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  // The harness runs children with WINUX_SHELL_GLOB=native, so the
  // in-process glob expands the pattern; it must scan the operand drive,
  // not the cwd drive, and open the matched file.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("g1240a.txt"), "MARKER-1240");
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

TEST(shred, shred_u_dialect_operand_removes_operand_drive_file) {
  TempDir tmp;
  tmp.write("v1240.txt", "MARKER-1240");
  const std::wstring operand =
      shred_dialect_detail::slash_drive_form(tmp.path / L"v1240.txt");
  const std::wstring shadow = shred_dialect_detail::shadow_path(
      tmp, shred_dialect_detail::drive_letter(tmp.path), L"v1240.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-u", L"-n", L"1", operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("shred.exe", L"-u", L"-n", L"1", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("shred stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"v1240.txt"));
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(shadow)));
}

// [GNU] #1348: --remove=wipe must rename the target to a fresh
// unpredictable name and delete only that name.  The old code renamed
// onto the compile-time constant "f6d4b2907e5c3a1", ignored the rename
// result, and deleted that path unconditionally: a pre-existing file
// with the constant name was destroyed while the target survived.
TEST(shred, shred_wipe_spares_preexisting_constant_named_file_issue1348) {
  TempDir tmp;
  tmp.mkdir("sub");
  tmp.write("sub/f6d4b2907e5c3a1", "keepme");
  tmp.write("sub/victim", "secret");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"--remove=wipe", L"sub/victim"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // The sentinel is untouched and the target is gone.
  EXPECT_TRUE(std::filesystem::exists(tmp.path / L"sub" / L"f6d4b2907e5c3a1"));
  EXPECT_EQ(tmp.read("sub/f6d4b2907e5c3a1"), "keepme");
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"sub" / L"victim"));

  // Without a collision the wipe path still removes its target, and
  // the default (-u / wipesync) mode spares the sentinel as well.
  tmp.write("sub/victim2", "secret2");
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"shred.exe", {L"-u", L"sub/victim2"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / L"sub" / L"victim2"));
  EXPECT_EQ(tmp.read("sub/f6d4b2907e5c3a1"), "keepme");
}

// [GNU] #1348: -s N shreds exactly N bytes — a file shorter than N
// grows to exactly N (no block round-up on an explicit size), and a
// longer file keeps its size.
TEST(shred, shred_size_limit_is_exact_issue1348) {
  TempDir tmp;
  tmp.write("short.bin", "12345");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-s", L"100", L"short.bin"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(static_cast<unsigned long long>(
                std::filesystem::file_size(tmp.path / L"short.bin")),
            100ULL);

  tmp.write("long.bin", std::string(600, 'x'));
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"shred.exe", {L"-s", L"100", L"long.bin"});
  auto r2 = p2.run();
  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ(static_cast<unsigned long long>(
                std::filesystem::file_size(tmp.path / L"long.bin")),
            600ULL);
}

// [GNU] #1348: -s accepts the coreutils suffix grammar (KB = 1000,
// KiB/K = 1024, b = 512); invalid sizes are rejected before any file
// is touched, with GNU's diagnostic.
TEST(shred, shred_size_suffix_forms_issue1348) {
  TempDir tmp;
  tmp.write("kb.bin", "");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-s", L"1KB", L"kb.bin"});
  EXPECT_EQ(p.run().exit_code, 0);
  EXPECT_EQ(static_cast<unsigned long long>(
                std::filesystem::file_size(tmp.path / L"kb.bin")),
            1000ULL);

  tmp.write("kib.bin", "");
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"shred.exe", {L"-s", L"1KiB", L"kib.bin"});
  EXPECT_EQ(p2.run().exit_code, 0);
  EXPECT_EQ(static_cast<unsigned long long>(
                std::filesystem::file_size(tmp.path / L"kib.bin")),
            1024ULL);

  tmp.write("keep.bin", "untouched");
  Pipeline p3;
  p3.set_cwd(tmp.wpath());
  p3.add(L"shred.exe", {L"-s", L"bogus", L"keep.bin"});
  auto r3 = p3.run();
  EXPECT_EQ(r3.exit_code, 1);
  EXPECT_TRUE(r3.stderr_text.find("invalid file size: 'bogus'") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("keep.bin"), "untouched");
}

// [GNU] #1348: a negative pass count is rejected up front; the file
// is not touched.
TEST(shred, shred_negative_passes_rejected_issue1348) {
  TempDir tmp;
  tmp.write("victim.txt", "still here");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-n", L"-1", L"victim.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid number of passes: '-1'") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("victim.txt"), "still here");
}
