// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

bool create_directory_junction(const std::filesystem::path& link,
                               const std::filesystem::path& target) {
  std::wstring command = L"cmd /d /c mklink /j \"" + link.wstring() + L"\" \"" +
                         target.wstring() + L"\" >nul";
  return _wsystem(command.c_str()) == 0;
}

bool set_readonly(const std::filesystem::path& path) {
  DWORD attrs = GetFileAttributesW(path.wstring().c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return SetFileAttributesW(path.wstring().c_str(),
                            attrs | FILE_ATTRIBUTE_READONLY) != FALSE;
}

std::wstring to_slash_drive_path(const std::filesystem::path& path) {
  std::wstring absolute = std::filesystem::absolute(path).wstring();
  if (absolute.size() < 3 || absolute[1] != L':' ||
      (absolute[2] != L'\\' && absolute[2] != L'/')) {
    return {};
  }

  std::wstring converted;
  converted.push_back(L'/');
  wchar_t drive = absolute[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  converted.push_back(drive);
  for (size_t i = 2; i < absolute.size(); ++i) {
    converted.push_back(absolute[i] == L'\\' ? L'/' : absolute[i]);
  }
  return converted;
}

}  // namespace

TEST(rm, rm_basic) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"file.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the file was removed
  bool file_exists = std::filesystem::exists(tmp.path / "file.txt");
  EXPECT_TRUE(!file_exists);
}

TEST(rm, rm_recursive) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir1");
  tmp.write("dir1/file.txt", "content");

  TEST_LOG_FILE_CONTENT("dir1/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"dir1"});

  TEST_LOG_CMD_LIST("rm.exe", L"-r", L"dir1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory and its contents were removed
  bool dir_exists = std::filesystem::exists(tmp.path / "dir1");
  EXPECT_TRUE(!dir_exists);
}

TEST(rm, rm_recursive_accepts_trailing_separator_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir1");
  tmp.write("dir1/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"-f", L"dir1/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir1"));
}

TEST(rm, rm_recursive_accepts_slash_drive_absolute_operand) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "dir1" / "subdir");
  tmp.write("dir1/subdir/file.txt", "content");

  std::wstring operand = to_slash_drive_path(tmp.path / "dir1");
  EXPECT_FALSE(operand.empty());
  if (operand.empty()) return;

  Pipeline p;
  p.set_cwd(std::filesystem::current_path().wstring());
  p.add(L"rm.exe", {L"-r", L"-f", operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir1"));
}

TEST(rm, rm_file_with_trailing_separator_reports_not_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"-f", L"file.txt/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(rm, rm_recursive_removes_readonly_file) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir1");
  tmp.write("dir1/file.txt", "content");
  bool readonly_set = set_readonly(tmp.path / "dir1" / "file.txt");
  EXPECT_TRUE(readonly_set);
  if (!readonly_set) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"-f", L"dir1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir1"));
}

TEST(rm, rm_recursive_removes_readonly_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "dir1" / "subdir");
  tmp.write("dir1/subdir/file.txt", "content");
  bool readonly_set = set_readonly(tmp.path / "dir1" / "subdir");
  EXPECT_TRUE(readonly_set);
  if (!readonly_set) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"-f", L"dir1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "dir1"));
}

TEST(rm, rm_recursive_removes_directory_junction_without_deleting_target) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "target");
  tmp.write("tree/target/keep.txt", "content");
  bool created = create_directory_junction(tmp.path / "tree" / "link",
                                           tmp.path / "tree" / "target");
  EXPECT_TRUE(created);
  if (!created) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"-f", L"tree\\link"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "tree" / "link"));
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / "tree" / "target" / "keep.txt"));
}

TEST(rm, rm_recursive_refuses_current_or_parent_directory_operands) {
  TempDir current_dir;
  current_dir.write("keep.txt", "content");

  Pipeline current_pipeline;
  current_pipeline.set_cwd(current_dir.wpath());
  current_pipeline.add(L"rm.exe", {L"-r", L"-f", L"."});
  auto current_result = current_pipeline.run();

  EXPECT_EQ(current_result.exit_code, 1);
  EXPECT_TRUE(current_result.stderr_text.find("refusing to remove") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(current_dir.path / "keep.txt"));

  TempDir parent_dir;
  parent_dir.write("keep.txt", "content");

  Pipeline parent_pipeline;
  parent_pipeline.set_cwd(parent_dir.wpath());
  parent_pipeline.add(L"rm.exe", {L"-r", L"-f", L".."});
  auto parent_result = parent_pipeline.run();

  EXPECT_EQ(parent_result.exit_code, 1);
  EXPECT_TRUE(parent_result.stderr_text.find("refusing to remove") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(parent_dir.path / "keep.txt"));
}

TEST(rm, rm_force) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-f", L"file.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"-f", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the file was removed
  bool file_exists = std::filesystem::exists(tmp.path / "file.txt");
  EXPECT_TRUE(!file_exists);
}

TEST(rm, rm_force_with_no_operands_succeeds) {
  Pipeline p;
  p.add(L"rm.exe", {L"-f"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(rm, rm_missing_operand_reports_help_hint_on_stderr) {
  Pipeline p;
  p.add(L"rm.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "rm: missing file operand\n"
                 "Try 'rm --help' for more information.\n");
}

TEST(rm, rm_dir_removes_empty_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "empty");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-d", L"empty"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "empty"));
}

TEST(rm, rm_dir_rejects_nonempty_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "nonempty");
  tmp.write("nonempty/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-d", L"nonempty"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "nonempty"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "nonempty/file.txt"));
}

TEST(rm, rm_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("file3.txt", "content3");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("file3.txt", "content3");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"file1.txt", L"file2.txt", L"file3.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"file1.txt", L"file2.txt", L"file3.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify all files were removed
  bool file1_exists = std::filesystem::exists(tmp.path / "file1.txt");
  bool file2_exists = std::filesystem::exists(tmp.path / "file2.txt");
  bool file3_exists = std::filesystem::exists(tmp.path / "file3.txt");
  EXPECT_TRUE(!file1_exists);
  EXPECT_TRUE(!file2_exists);
  EXPECT_TRUE(!file3_exists);
}

TEST(rm, rm_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("keep.log", "log content");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("keep.log", "log content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify .txt files were removed but .log file remains
  bool txt1_exists = std::filesystem::exists(tmp.path / "file1.txt");
  bool txt2_exists = std::filesystem::exists(tmp.path / "file2.txt");
  bool log_exists = std::filesystem::exists(tmp.path / "keep.log");
  EXPECT_TRUE(!txt1_exists);
  EXPECT_TRUE(!txt2_exists);
  EXPECT_TRUE(log_exists);
}

TEST(rm, rm_interactive_once_declines_bulk_remove) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("file3.txt", "content3");
  tmp.write("file4.txt", "content4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"rm.exe",
        {L"-I", L"file1.txt", L"file2.txt", L"file3.txt", L"file4.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file1.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file2.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file3.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file4.txt"));
}

TEST(rm, rm_interactive_never_does_not_prompt) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"--interactive=never", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("remove 'file.txt'?") == std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(rm, rm_interactive_accepts_gnu_aliases) {
  TempDir no_alias;
  no_alias.write("file.txt", "content");

  Pipeline no_pipeline;
  no_pipeline.set_cwd(no_alias.wpath());
  no_pipeline.set_stdin("y\n");
  no_pipeline.add(L"rm.exe", {L"--interactive=no", L"file.txt"});
  auto no_result = no_pipeline.run();

  EXPECT_EQ(no_result.exit_code, 0);
  EXPECT_TRUE(no_result.stderr_text.find("remove 'file.txt'?") ==
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(no_alias.path / "file.txt"));

  TempDir yes_alias;
  yes_alias.write("file.txt", "content");

  Pipeline yes_pipeline;
  yes_pipeline.set_cwd(yes_alias.wpath());
  yes_pipeline.set_stdin("n\n");
  yes_pipeline.add(L"rm.exe", {L"--interactive=yes", L"file.txt"});
  auto yes_result = yes_pipeline.run();

  EXPECT_EQ(yes_result.exit_code, 0);
  EXPECT_TRUE(yes_result.stderr_text.find("remove 'file.txt'?") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(yes_alias.path / "file.txt"));

  TempDir none_alias;
  none_alias.write("file.txt", "content");

  Pipeline none_pipeline;
  none_pipeline.set_cwd(none_alias.wpath());
  none_pipeline.add(L"rm.exe", {L"--interactive=none", L"file.txt"});
  auto none_result = none_pipeline.run();

  EXPECT_EQ(none_result.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(none_alias.path / "file.txt"));
}

TEST(rm, rm_interactive_always_declines_single_remove) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"rm.exe", {L"--interactive=always", L"file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("remove 'file.txt'?") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(rm, rm_interactive_once_accepts_bulk_remove) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("file3.txt", "content3");
  tmp.write("file4.txt", "content4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("y\n");
  p.add(L"rm.exe", {L"--interactive=once", L"file1.txt", L"file2.txt",
                    L"file3.txt", L"file4.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("remove 4 arguments?") != std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "file1.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "file2.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "file3.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "file4.txt"));
}

TEST(rm, rm_interactive_invalid_when_fails) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"--interactive=sometimes", L"file.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid argument 'sometimes'") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file.txt"));
}

TEST(rm, rm_interactive_force_last_option_wins) {
  TempDir force_last;
  force_last.write("file.txt", "content");

  Pipeline force_pipeline;
  force_pipeline.set_cwd(force_last.wpath());
  force_pipeline.set_stdin("n\n");
  force_pipeline.add(L"rm.exe", {L"-i", L"-f", L"file.txt"});

  auto force_result = force_pipeline.run();

  EXPECT_EQ(force_result.exit_code, 0);
  EXPECT_TRUE(force_result.stderr_text.find("remove 'file.txt'?") ==
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(force_last.path / "file.txt"));

  TempDir interactive_last;
  interactive_last.write("file.txt", "content");

  Pipeline interactive_pipeline;
  interactive_pipeline.set_cwd(interactive_last.wpath());
  interactive_pipeline.set_stdin("n\n");
  interactive_pipeline.add(L"rm.exe", {L"-f", L"-i", L"file.txt"});

  auto interactive_result = interactive_pipeline.run();

  EXPECT_EQ(interactive_result.exit_code, 0);
  EXPECT_TRUE(interactive_result.stderr_text.find("remove 'file.txt'?") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(interactive_last.path / "file.txt"));
}

TEST(rm, rm_interactive_once_and_force_last_option_wins) {
  TempDir force_last;
  force_last.write("file1.txt", "content1");
  force_last.write("file2.txt", "content2");
  force_last.write("file3.txt", "content3");
  force_last.write("file4.txt", "content4");

  Pipeline force_pipeline;
  force_pipeline.set_cwd(force_last.wpath());
  force_pipeline.set_stdin("n\n");
  force_pipeline.add(L"rm.exe", {L"-I", L"-f", L"file1.txt", L"file2.txt",
                                 L"file3.txt", L"file4.txt"});

  auto force_result = force_pipeline.run();

  EXPECT_EQ(force_result.exit_code, 0);
  EXPECT_TRUE(force_result.stderr_text.find("remove 4 arguments?") ==
              std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(force_last.path / "file1.txt"));
  EXPECT_FALSE(std::filesystem::exists(force_last.path / "file2.txt"));
  EXPECT_FALSE(std::filesystem::exists(force_last.path / "file3.txt"));
  EXPECT_FALSE(std::filesystem::exists(force_last.path / "file4.txt"));

  TempDir once_last;
  once_last.write("file1.txt", "content1");
  once_last.write("file2.txt", "content2");
  once_last.write("file3.txt", "content3");
  once_last.write("file4.txt", "content4");

  Pipeline once_pipeline;
  once_pipeline.set_cwd(once_last.wpath());
  once_pipeline.set_stdin("n\n");
  once_pipeline.add(L"rm.exe", {L"-f", L"-I", L"file1.txt", L"file2.txt",
                                L"file3.txt", L"file4.txt"});

  auto once_result = once_pipeline.run();

  EXPECT_EQ(once_result.exit_code, 0);
  EXPECT_TRUE(once_result.stderr_text.find("remove 4 arguments?") !=
              std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(once_last.path / "file1.txt"));
  EXPECT_TRUE(std::filesystem::exists(once_last.path / "file2.txt"));
  EXPECT_TRUE(std::filesystem::exists(once_last.path / "file3.txt"));
  EXPECT_TRUE(std::filesystem::exists(once_last.path / "file4.txt"));
}

// ===== #1297 regression: on-disk directory named NUL =====

TEST(rm, rm_recursive_removes_on_disk_nul_directory_issue1297) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "pa");

  // A directory literally named NUL can only be created through the
  // extended (\\?\) spelling; the plain spelling names the DOS device.
  const std::filesystem::path ext_dir =
      std::filesystem::path(L"\\\\?\\" + tmp.wpath() + L"\\pa\\NUL");
  ASSERT_TRUE(std::filesystem::create_directory(ext_dir));
  {
    std::ofstream inside(ext_dir / "inside.txt");
    inside << "payload";
  }
  ASSERT_TRUE(std::filesystem::exists(ext_dir / "inside.txt"));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-rf", tmp.wpath() + L"\\pa\\NUL"});

  auto r = p.run();

  // The operand names the on-disk directory (only a bare NUL names the
  // device), so rm -rf must really remove it: exit 0 AND the directory
  // actually gone. The #1283 audit observed exit 0 with the directory
  // left in place (false success).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(std::filesystem::exists(ext_dir));
}

TEST(rm, rm_force_bare_nul_operand_names_device_not_filesystem_issue1297) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-rf", L"NUL"});

  auto r = p.run();

  // Bare NUL is the null device, never a filesystem entry: rm -rf must
  // not create or delete anything, and must not hang or crash.
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
  EXPECT_TRUE(std::filesystem::is_empty(tmp.path));
}

// ===== TEMPORARY DIAGNOSTIC for #1297 (remove before merge) =====
// Probes, from a real Windows process, how each Win32 API treats an
// on-disk directory named NUL under the extended (\\?\) spelling, so the
// rm fix can target the step that actually loses the directory. Always
// fails on purpose: the dump is the result.
TEST(rm, diag_nul_dir_api_probe_issue1297) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "pa");
  const std::wstring plain = tmp.wpath() + L"\\pa\\NUL";
  const std::wstring ext = L"\\\\?\\" + plain;
  const std::filesystem::path ext_dir = std::filesystem::path(ext);
  ASSERT_TRUE(std::filesystem::create_directory(ext_dir));
  {
    std::ofstream inside(ext_dir / "inside.txt");
    inside << "payload";
  }

  std::string out;
  auto narrow = [](const std::wstring& w) {
    if (w.empty()) return std::string{};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr,
                                0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n,
                        nullptr, nullptr);
    return s;
  };
  auto hex = [](DWORD v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)v);
    return std::string(buf);
  };
  auto num = [](auto v) { return std::to_string(v); };

  out += "plain=" + narrow(plain) + "\n";
  DWORD a_plain = GetFileAttributesW(plain.c_str());
  out += "GFAW(plain)=" + hex(a_plain) + " err=" + num(GetLastError()) + "\n";
  DWORD a_ext = GetFileAttributesW(ext.c_str());
  out += "GFAW(ext)=" + hex(a_ext) + " err=" + num(GetLastError()) + "\n";

  HANDLE h1 = CreateFileW(ext.c_str(), FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                          nullptr);
  out += "CreateFile(ext,READ_ATTRS)=" + num(h1 != INVALID_HANDLE_VALUE) +
         " err=" + num(GetLastError()) + "\n";
  if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
  HANDLE h2 = CreateFileW(ext.c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                          nullptr);
  out += "CreateFile(ext,GENERIC_READ)=" + num(h2 != INVALID_HANDLE_VALUE) +
         " err=" + num(GetLastError()) + "\n";
  if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);

  WIN32_FIND_DATAW fd{};
  HANDLE hf = FindFirstFileW((ext + L"\\*").c_str(), &fd);
  out += "FFF(ext\\*)=" + num(hf != INVALID_HANDLE_VALUE) +
         " err=" + num(GetLastError());
  if (hf != INVALID_HANDLE_VALUE) {
    out += " first=" + narrow(fd.cFileName);
    FindClose(hf);
  }
  out += "\n";

  HANDLE hp = FindFirstFileW((tmp.wpath() + L"\\pa\\*").c_str(), &fd);
  out += "FFF(parent\\*)=" + num(hp != INVALID_HANDLE_VALUE) + " entries:";
  if (hp != INVALID_HANDLE_VALUE) {
    do {
      out += " [" + narrow(fd.cFileName) +
             " attr=" + hex(fd.dwFileAttributes) + "]";
    } while (FindNextFileW(hp, &fd));
    FindClose(hp);
  }
  out += "\n";

  // Control: std::filesystem::remove_all on a sacrificial NUL dir.
  std::filesystem::create_directory(tmp.path / "qb");
  const std::wstring ext2 = L"\\\\?\\" + tmp.wpath() + L"\\qb\\NUL";
  const std::filesystem::path ext2_dir = std::filesystem::path(ext2);
  std::filesystem::create_directory(ext2_dir);
  {
    std::ofstream inside(ext2_dir / "inside.txt");
    inside << "payload";
  }
  std::error_code ec;
  auto removed = std::filesystem::remove_all(ext2_dir, ec);
  out += "remove_all(ext2)=" + num(removed) + " ec=" + num(ec.value()) +
         " exists_after=" + num(std::filesystem::exists(ext2_dir)) + "\n";

  // Direct RemoveDirectoryW on an empty sacrificial NUL dir.
  std::filesystem::create_directory(tmp.path / "qc");
  const std::wstring ext3 = L"\\\\?\\" + tmp.wpath() + L"\\qc\\NUL";
  std::filesystem::create_directory(std::filesystem::path(ext3));
  BOOL rd = RemoveDirectoryW(ext3.c_str());
  out += "RemoveDirectoryW(ext3 empty)=" + num((bool)rd) +
         " err=" + num(GetLastError()) + " exists_after=" +
         num(std::filesystem::exists(std::filesystem::path(ext3))) + "\n";

  FAIL() << "TEMP DIAGNOSTIC DUMP (#1297):\n" << out;
}

// ===== #1298 regression: rm -v output =====

TEST(rm, rm_verbose_reports_removed_file_issue1298) {
  TempDir tmp;
  tmp.write("f.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-v", L"f.txt"});

  auto r = p.run();

  // [GNU] rm -v prints "removed '<name>'" for each removal. rm had no
  // verbose coverage at all while mkdir/rmdir did; the #1283 audit
  // observed -v being silently ignored by all three commands.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "removed 'f.txt'\n");
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "f.txt"));
}

TEST(rm, rm_long_verbose_reports_removed_file_issue1298) {
  TempDir tmp;
  tmp.write("f.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"--verbose", L"f.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "removed 'f.txt'\n");
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "f.txt"));
}
