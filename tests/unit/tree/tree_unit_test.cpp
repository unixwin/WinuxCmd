// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <filesystem>

#include "framework/winuxtest.h"

TEST(tree, tree_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  std::filesystem::create_directories(tmp.path / "subdir");
  tmp.write("subdir/file3.txt", "content3");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("subdir/file3.txt", "content3");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {});

  TEST_LOG_CMD_LIST("tree.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify the output contains the expected files
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("subdir") != std::string::npos);
}

TEST(tree, tree_depth_limit) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "level1" / "level2");
  tmp.write("level1/level2/file.txt", "deep content");

  TEST_LOG_FILE_CONTENT("level1/level2/file.txt", "deep content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-L", L"1"});

  TEST_LOG_CMD_LIST("tree.exe", L"-L", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -L 1 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify level1 is shown but level2 is not
  EXPECT_TRUE(r.stdout_text.find("level1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("level2") == std::string::npos);
}

TEST(tree, tree_dirs_only) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  std::filesystem::create_directories(tmp.path / "dir1");
  std::filesystem::create_directories(tmp.path / "dir2");
  tmp.write("dir1/inside.txt", "inside");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT("dir1/inside.txt", "inside");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-d"});

  TEST_LOG_CMD_LIST("tree.exe", L"-d");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -d output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify only directories are shown
  EXPECT_TRUE(r.stdout_text.find("dir1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("dir2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(tree, tree_all_files) {
  TempDir tmp;
  tmp.write("normal.txt", "content");
  tmp.write(".hidden.txt", "hidden content");

  TEST_LOG_FILE_CONTENT("normal.txt", "content");
  TEST_LOG_FILE_CONTENT(".hidden.txt", "hidden content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-a"});

  TEST_LOG_CMD_LIST("tree.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -a output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify both files are shown
  EXPECT_TRUE(r.stdout_text.find("normal.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(tree, tree_full_path) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-f"});

  TEST_LOG_CMD_LIST("tree.exe", L"-f");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify full path is shown (should contain tmp path)
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
}

TEST(tree, tree_exclude_pattern) {
  TempDir tmp;
  tmp.write("file.txt", "content");
  tmp.write("test.tmp", "temp content");
  tmp.write("other.txt", "other content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");
  TEST_LOG_FILE_CONTENT("test.tmp", "temp content");
  TEST_LOG_FILE_CONTENT("other.txt", "other content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-I", L"*.tmp"});

  TEST_LOG_CMD_LIST("tree.exe", L"-I", L"*.tmp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -I output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify .tmp file is excluded
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("test.tmp") == std::string::npos);
}

TEST(tree, tree_include_pattern) {
  TempDir tmp;
  tmp.write("file.cpp", "c++ content");
  tmp.write("file.txt", "text content");
  tmp.write("file.py", "python content");

  TEST_LOG_FILE_CONTENT("file.cpp", "c++ content");
  TEST_LOG_FILE_CONTENT("file.txt", "text content");
  TEST_LOG_FILE_CONTENT("file.py", "python content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-P", L"*.cpp"});

  TEST_LOG_CMD_LIST("tree.exe", L"-P", L"*.cpp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify only .cpp files are shown
  EXPECT_TRUE(r.stdout_text.find("file.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.py") == std::string::npos);
}

TEST(tree, tree_show_size) {
  TempDir tmp;
  tmp.write("small.txt", "abc");
  tmp.write("large.txt", std::string(1000, 'x'));

  TEST_LOG_FILE_CONTENT("small.txt", "abc");
  TEST_LOG_FILE_CONTENT("large.txt", std::string(1000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-s"});

  TEST_LOG_CMD_LIST("tree.exe", L"-s");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify size is shown (should contain brackets with size)
  EXPECT_TRUE(r.stdout_text.find("[") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("]") != std::string::npos);
}

TEST(tree, tree_sort_by_time) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("new.txt", "new");

  TEST_LOG_FILE_CONTENT("old.txt", "old");
  TEST_LOG_FILE_CONTENT("new.txt", "new");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"-t"});

  TEST_LOG_CMD_LIST("tree.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe -t output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Verify files are listed
  EXPECT_TRUE(r.stdout_text.find("old.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("new.txt") != std::string::npos);
}

TEST(tree, tree_single_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "mydir");
  tmp.write("mydir/file.txt", "content");

  TEST_LOG_FILE_CONTENT("mydir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tree.exe", {L"mydir"});

  TEST_LOG_CMD_LIST("tree.exe", L"mydir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree.exe mydir output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("mydir") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
}

// [#1214] `tree -o FILE` opened the target in text mode and dropped the
// trailing "N directories, M files" summary, so a redirected listing was
// neither LF-only nor complete next to the console listing.
TEST(tree, tree_output_file_is_lf_only_and_keeps_summary_issue1214) {
  TempDir tmp;
  tmp.write("dir1/a.txt", "x");

  Pipeline console;
  console.set_cwd(tmp.wpath());
  console.add(L"tree.exe", {L"dir1"});
  auto rc = console.run();

  EXPECT_EXIT_CODE(rc, 0);
  TEST_LOG("tree stdout", rc.stdout_text);
  EXPECT_TRUE(rc.stdout_text.find('\r') == std::string::npos);
  EXPECT_TRUE(rc.stdout_text.find("directories, ") != std::string::npos);

  Pipeline file;
  file.set_cwd(tmp.wpath());
  file.add(L"tree.exe", {L"-o", L"out.txt", L"dir1"});
  auto rf = file.run();

  EXPECT_EXIT_CODE(rf, 0);
  const std::string out = tmp.read("out.txt");
  TEST_LOG("tree -o bytes", out);
  EXPECT_TRUE(out.find('\r') == std::string::npos);
  EXPECT_TRUE(out.find('\n') != std::string::npos);
  EXPECT_TRUE(out.find("directories, ") != std::string::npos);
}

// ===== #1236 regression: POSIX drive-dialect root operands =====
//
// `tree /e/Data` handed the raw "/e/..." text to GetFullPathNameW, and Win32
// resolves a leading "/e" against the CURRENT drive: from a C: cwd the
// command rejected a directory that exists on E: - or silently listed the
// shadow "C:\e\Data" where one happened to exist. The root operand must fold
// through the shared normalize_api_operand boundary before any Win32 call.
// The probes below synthesize the dialect spelling from the TempDir's own
// drive, so the assertions work on any host regardless of which drives exist.
namespace tree_dialect_detail {

// Lowercase drive letter of an absolute path ("C:\x" -> L'c').
auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

// "C:\Users\...\wct" -> "/c/Users/.../wct" (MSYS drive spelling).
auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..."
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

// "/cygdrive/c/Users/..." spelling.
auto cygdrive_form(const std::filesystem::path &path) -> std::wstring {
  return L"/cygdrive" + slash_drive_form(path);
}

// "/mnt/c/Users/..." spelling.
auto mnt_form(const std::filesystem::path &path) -> std::wstring {
  return L"/mnt" + slash_drive_form(path);
}

// UTF-8 form of the operand for exact message comparisons.
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

// Keep the shell-namespace mapping out of the way: the dialect fold must land
// on the operand drive with the shell root unconfigured too.
void clear_shell_root_env(Pipeline &pipeline) {
  for (const auto *name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    pipeline.set_env(name, L"");
  }
}

}  // namespace tree_dialect_detail

TEST(tree, tree_drive_dialect_operand_lists_operand_drive) {
  TempDir tmp;
  tmp.write("wb-marker.txt", "x");
  std::filesystem::create_directories(tmp.path / "wbsub");
  tmp.write("wbsub/inner.txt", "y");
  const std::wstring operand = tree_dialect_detail::slash_drive_form(tmp.path);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  tree_dialect_detail::clear_shell_root_env(p);
  p.add(L"tree.exe", {L"-L", L"1", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("tree.exe", L"-L", L"1", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree drive dialect stdout", r.stdout_text);

  // Before #1236 this rejected the operand (or listed a cwd-drive shadow).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "wb-marker.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "cannot access");
}

TEST(tree, tree_cygdrive_and_mnt_operands_list_operand_drive) {
  TempDir tmp;
  tmp.write("wb-marker.txt", "x");

  for (const std::wstring operand :
       {tree_dialect_detail::cygdrive_form(tmp.path),
        tree_dialect_detail::mnt_form(tmp.path)}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    tree_dialect_detail::clear_shell_root_env(p);
    p.add(L"tree.exe", {operand});
    auto r = p.run();

    TEST_LOG_CMD_LIST("tree.exe", operand);
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("tree cygdrive/mnt stdout", r.stdout_text);

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_CONTAINS(r.stdout_text, "wb-marker.txt");
    EXPECT_NOT_CONTAINS(r.stdout_text, "cannot access");
  }
}

TEST(tree, tree_bare_drive_root_operand_lists_drive_root) {
  TempDir tmp;
  const wchar_t drive = tree_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  tree_dialect_detail::clear_shell_root_env(p);
  p.add(L"tree.exe", {L"-L", L"1", operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("tree.exe", L"-L", L"1", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree bare drive root stdout", r.stdout_text);

  // "/x" names the root of the drive, not <cwd>:\x: the header is the drive
  // root itself and nothing is rejected.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text,
                  std::string(1, static_cast<char>(
                                     std::toupper(static_cast<unsigned char>(
                                         static_cast<char>(drive))))) +
                      ":\\");
  EXPECT_NOT_CONTAINS(r.stdout_text, "cannot access");
}

TEST(tree, tree_drive_dialect_from_foreign_cwd_lists_operand_drive) {
  TempDir tmp;
  tmp.write("wb-marker.txt", "x");
  const wchar_t other = tree_dialect_detail::other_fixed_drive(
      tree_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand = tree_dialect_detail::slash_drive_form(tmp.path);

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  tree_dialect_detail::clear_shell_root_env(p);
  p.add(L"tree.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("tree.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree foreign-cwd dialect stdout", r.stdout_text);

  // The audit's exact shadow shape: cwd on one drive, operand on another.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "wb-marker.txt");
  EXPECT_NOT_CONTAINS(r.stdout_text, "cannot access");
}

TEST(tree, tree_missing_dir_error_quotes_operand_spelling) {
  TempDir tmp;
  const std::wstring operand =
      tree_dialect_detail::slash_drive_form(tmp.path / L"absent1236");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  tree_dialect_detail::clear_shell_root_env(p);
  p.add(L"tree.exe", {operand});
  auto r = p.run();

  TEST_LOG_CMD_LIST("tree.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree missing-dir stdout", r.stdout_text);

  // [GNU] the message quotes what the user typed, never the folded native
  // form (MSYS prints the operand spelling, #1234 nit precedent).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stdout_text,
                  "cannot access '" + tree_dialect_detail::utf8(operand) + "'");
}

// ===== #1254 regression: the -o output operand folds like the read side =====
//
// The root operand folds through the shared normalize_api_operand boundary
// since #1236, but the -o file operand went to ofstream as raw text. Win32
// resolves a leading "/e" against the CURRENT drive, so from a foreign cwd
// `tree -o /e/probe/out.txt` opened a shadow "<cwd-drive>:\e\probe\out.txt"
// (or failed outright) instead of writing on the named drive. The output
// operand now folds through the same boundary; the error message keeps the
// spelling as typed.

TEST(tree, tree_output_operand_dialect_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t other = tree_dialect_detail::other_fixed_drive(
      tree_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }

  const std::wstring native_out = tmp.wpath() + L"\\wb-out.txt";
  const std::wstring drive_operand =
      tree_dialect_detail::slash_drive_form(tmp.path) + L"/wb-out.txt";
  const std::wstring cygdrive_operand =
      tree_dialect_detail::cygdrive_form(tmp.path) + L"/wb-out.txt";
  const std::wstring mnt_operand =
      tree_dialect_detail::mnt_form(tmp.path) + L"/wb-out.txt";

  for (const std::wstring operand :
       {drive_operand, cygdrive_operand, mnt_operand}) {
    // Foreign cwd: the audit's shadow shape - cwd on one drive, operand on
    // another. The listed directory is the small tmp dir on its own drive.
    Pipeline p;
    p.set_cwd(std::wstring(1, other) + L":\\");
    tree_dialect_detail::clear_shell_root_env(p);
    p.add(L"tree.exe", {L"-o", operand,
                        tree_dialect_detail::slash_drive_form(tmp.path)});
    auto r = p.run();

    TEST_LOG_CMD_LIST("tree.exe", L"-o", operand,
                      tree_dialect_detail::slash_drive_form(tmp.path));
    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("tree -o dialect stdout", r.stdout_text);

    // Before #1254 the ofstream landed on the cwd drive (shadow) or failed
    // to open; the file must exist at the operand's own native location.
    EXPECT_EQ(r.exit_code, 0);
    const std::string bytes = tmp.read("wb-out.txt");
    TEST_LOG("tree -o dialect file bytes", bytes);
    EXPECT_TRUE(bytes.find("directories, ") != std::string::npos);

    // Shadow negative: the pre-#1254 resolution of the operand against the
    // cwd drive must not have been written.
    const std::wstring shadow = std::wstring(1, other) + L":" + operand;
    const DWORD shadow_attrs = GetFileAttributesW(shadow.c_str());
    EXPECT_TRUE(shadow_attrs == INVALID_FILE_ATTRIBUTES);

    DeleteFileW(native_out.c_str());
  }
}

TEST(tree, tree_output_operand_open_failure_quotes_operand_spelling) {
  TempDir tmp;
  const std::wstring operand = tree_dialect_detail::slash_drive_form(tmp.path) +
                               L"/absent-dir1254/out.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  tree_dialect_detail::clear_shell_root_env(p);
  p.add(L"tree.exe", {L"-o", operand, L"."});
  auto r = p.run();

  TEST_LOG_CMD_LIST("tree.exe", L"-o", operand, L".");
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tree -o open failure stderr", r.stderr_text);

  // [GNU] the message quotes the operand spelling as typed (#1234 split).
  EXPECT_NE(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text,
                  "cannot open output file: " +
                      tree_dialect_detail::utf8(operand));
}
// #1374: tree expands its directory operands in
// WINUX_SHELL_GLOB=native mode.
TEST(tree, tree_native_glob_expands_operands_issue1374) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "gt");
  tmp.write("gt/t.txt", "T\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUX_SHELL_GLOB", L"native");
  p.add(L"tree.exe", {L"g*"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("t.txt") != std::string::npos);
}

