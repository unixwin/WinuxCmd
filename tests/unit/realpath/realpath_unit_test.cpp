// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

auto wide_to_utf8_for_realpath_test(const std::wstring& text) -> std::string {
  if (text.empty()) return {};
  int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), needed, nullptr, nullptr);
  return out;
}

auto utf8_to_wide_for_realpath_test(const std::string& text) -> std::wstring {
  if (text.empty()) return {};
  int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), needed);
  return out;
}

// [GNU] realpath prints the kernel-canonical spelling of every existing
// prefix (on-disk casing, long names instead of 8.3 aliases).  Re-express
// the longest existing prefix of an expected path the same way so the
// comparison is stable on systems where temp paths contain short names.
auto long_existing_prefix_path(std::filesystem::path path)
    -> std::filesystem::path {
  std::vector<std::filesystem::path> suffix;
  std::error_code ec;
  while (!path.empty() && !std::filesystem::exists(path, ec)) {
    suffix.push_back(path.filename());
    auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
    ec.clear();
  }

  HANDLE handle =
      CreateFileW(path.wstring().c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    DWORD needed = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (needed > 0) {
      std::wstring buffer(needed + 1, L'\0');
      DWORD written = GetFinalPathNameByHandleW(
          handle, buffer.data(), static_cast<DWORD>(buffer.size()),
          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      if (written > 0 && written < buffer.size()) {
        buffer.resize(written);
        if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) {
          path = L"\\\\" + buffer.substr(8);
        } else if (buffer.rfind(L"\\\\?\\", 0) == 0 ||
                   buffer.rfind(L"\\??\\", 0) == 0) {
          path = buffer.substr(4);
        } else {
          path = buffer;
        }
      }
    }
    CloseHandle(handle);
  } else {
    std::wstring base = path.wstring();
    DWORD needed = GetLongPathNameW(base.c_str(), nullptr, 0);
    if (needed > 0) {
      std::wstring buffer(needed, L'\0');
      DWORD written = GetLongPathNameW(base.c_str(), buffer.data(), needed);
      if (written > 0 && written < needed) {
        buffer.resize(written);
        path = buffer;
      }
    }
  }

  for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
    path /= *it;
  }
  return path;
}

auto normalize_path_text(std::string text) -> std::string {
  std::string terminator;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
    terminator.insert(terminator.begin(), text.back());
    text.pop_back();
  }

  std::replace(text.begin(), text.end(), '/', '\\');
  if (text.rfind("\\\\?\\UNC\\", 0) == 0) {
    text = "\\\\" + text.substr(8);
  } else if (text.rfind("\\\\?\\", 0) == 0 || text.rfind("\\??\\", 0) == 0) {
    text.erase(0, 4);
  }

  auto wide = utf8_to_wide_for_realpath_test(text);
  if (!wide.empty()) {
    auto normalized = long_existing_prefix_path(std::filesystem::path(wide));
    auto utf8 = wide_to_utf8_for_realpath_test(normalized.wstring());
    if (!utf8.empty()) {
      text = utf8;
    }
  }

  return text + terminator;
}

}  // namespace

TEST(realpath, realpath_basic) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"file.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Output should be an absolute path
  EXPECT_TRUE(r.stdout_text.find("file.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(":\\") != std::string::npos ||
              r.stdout_text.find(":/") != std::string::npos);
}

TEST(realpath, realpath_requires_operand) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {});

  TEST_LOG_CMD_LIST("realpath.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "realpath: missing operand\nTry 'realpath --help' for more "
                 "information.\n");
}

TEST(realpath, realpath_strip) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-s", L"subdir"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-s", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -s output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should not end with separator
  EXPECT_FALSE(r.stdout_text.ends_with("\\") || r.stdout_text.ends_with("/"));
}

TEST(realpath, realpath_nonexistent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe nonexistent output", r.stdout_text);

  // realpath on Windows can resolve paths even if file doesn't exist
  // The behavior depends on the implementation
}

TEST(realpath, realpath_rejects_empty_operand) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L""});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "realpath: '': No such file or directory\n");
}

TEST(realpath, realpath_rejects_empty_relative_to) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"--relative-to", L"", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "realpath: invalid operand: empty string\n");
}

TEST(realpath, realpath_rejects_empty_relative_base) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"--relative-base", L"", L"--relative-to=.", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "realpath: invalid operand: empty string\n");
}

TEST(realpath, realpath_canonicalize_existing) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-e", L"subdir/file.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-e", L"subdir/file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -e output", r.stdout_text);
  TEST_LOG("realpath.exe -e stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      normalize_path_text((tmp.path / "subdir/file.txt").string()) + "\n");
}

TEST(realpath, realpath_canonicalize_existing_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-e", L"missing.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-e", L"missing.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -e missing stdout", r.stdout_text);
  TEST_LOG("realpath.exe -e missing stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] realpath reports the operand via quotef(): simple names are
  // unquoted — "realpath: missing.txt: No such file or directory".
  EXPECT_EQ_TEXT(r.stderr_text,
                 "realpath: missing.txt: No such file or directory\n");
}

TEST(realpath, realpath_canonicalize_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-m", L"missing/path/file.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-m", L"missing/path/file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -m output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                 normalize_path_text(
                     (tmp.path / "missing/path/file.txt").string() + "\n"));
}

TEST(realpath, realpath_default_requires_existing_parent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"missing/path/file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(realpath, realpath_canonicalize_allows_missing_leaf) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-E", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "missing.txt").string() + "\n"));
}

TEST(realpath, realpath_canonicalize_mode_last_occurrence_wins_to_existing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-m", L"-e", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] realpath reports the operand via quotef(): simple names are
  // unquoted — "realpath: missing.txt: No such file or directory".
  EXPECT_EQ_TEXT(r.stderr_text,
                 "realpath: missing.txt: No such file or directory\n");
}

TEST(realpath, realpath_canonicalize_mode_last_occurrence_wins_to_missing) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-e", L"-m", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "missing.txt").string() + "\n"));
}

TEST(realpath, realpath_relative_to) {
  TempDir tmp;
  tmp.mkdir("base");
  tmp.write("base/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe",
        {L"--relative-to", L"base", L"base\\..\\base\\file.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text), "file.txt\n");
}

TEST(realpath, realpath_existing_relative_to_requires_directory) {
  TempDir tmp;
  tmp.mkdir("dir1");
  tmp.write("dir1/f", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe",
        {L"-e", L"--relative-base=.", L"--relative-to=dir1\\f", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
}

TEST(realpath, realpath_existing_relative_base_requires_directory) {
  TempDir tmp;
  tmp.mkdir("dir1");
  tmp.write("dir1/f", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe",
        {L"-e", L"--relative-base=dir1\\f", L"--relative-to=.", L"."});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
}

TEST(realpath, realpath_relative_base_leaves_outside_absolute) {
  TempDir tmp;
  tmp.mkdir("base");
  tmp.write("outside.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"--relative-base", L"base", L"outside.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "outside.txt").string() + "\n"));
}

TEST(realpath, realpath_zero_delimiter) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-z", L"file.txt"});

  TEST_LOG_CMD_LIST("realpath.exe", L"-z", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath.exe -z output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  std::string expected = (tmp.path / "file.txt").string();
  expected.push_back('\0');
  EXPECT_EQ(normalize_path_text(r.stdout_text), normalize_path_text(expected));
}

namespace {
bool realpath_test_create_symlink(const std::filesystem::path& link,
                                  const std::filesystem::path& target,
                                  bool target_is_directory = false) {
  DWORD flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (target_is_directory) {
    flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  }
  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }
  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}
}  // namespace

TEST(realpath, realpath_resolves_intermediate_symlink_component) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "real");
  tmp.write("real/f.txt", "content");

  std::filesystem::path link = tmp.path / "ldir";
  if (!realpath_test_create_symlink(link, tmp.path / "real", true)) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"ldir/f.txt"});
  auto r = p.run();

  // [GNU] Intermediate symlink components are resolved: "ldir/f.txt" prints
  // the "real/f.txt" path (issue #985).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "real" / "f.txt").string() + "\n"));
}

TEST(realpath, realpath_dangling_leaf_resolves_target_by_default) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!realpath_test_create_symlink(link,
                                    std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"dang"});
  auto r = p.run();

  // [GNU] Default mode resolves a dangling leaf link to its target path.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "z_absent_tgt").string() + "\n"));
}

TEST(realpath, realpath_existing_mode_fails_on_dangling_link) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!realpath_test_create_symlink(link,
                                    std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-e", L"dang"});
  auto r = p.run();

  // [GNU] -e requires every component to exist: a dangling link target
  // fails with "No such file or directory", exit 1 (issue #986).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("No such file or directory") !=
              std::string::npos);
}

TEST(realpath, realpath_missing_mode_allows_dangling_link) {
  TempDir tmp;

  std::filesystem::path link = tmp.path / "dang";
  if (!realpath_test_create_symlink(link,
                                    std::filesystem::path(L"z_absent_tgt"))) {
    return;
  }

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-m", L"dang"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((tmp.path / "z_absent_tgt").string() + "\n"));
}

TEST(realpath, realpath_component_too_long_fails) {
  TempDir tmp;
  const std::wstring long_name(300, L'a');

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {long_name});
  auto r = p.run();

  // [GNU] A component exceeding NAME_MAX fails with ENAMETOOLONG even as
  // the last component: "realpath: aaa...: File name too long" (#370).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("File name too long") != std::string::npos);
}

TEST(realpath, realpath_missing_mode_allows_too_long_component) {
  TempDir tmp;
  const std::wstring long_name(300, L'a');

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"-m", long_name});
  auto r = p.run();

  // [GNU] -m does not probe components, so an overlong leaf is appended
  // verbatim and resolves successfully (#370).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(std::string(300, 'a')) != std::string::npos);
}

TEST(realpath, realpath_through_regular_file_is_not_a_directory) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"file.txt/inner"});
  auto r = p.run();

  // [GNU] Traversing through a non-directory reports ENOTDIR even when the
  // remaining component is the last one.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
}

TEST(realpath, realpath_missing_leaf_with_trailing_separator_resolves) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"missing/"});
  auto r = p.run();

  // [GNU] The last-component exemption ignores trailing slashes: a missing
  // leaf followed only by separators still canonicalizes.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                 normalize_path_text((tmp.path / "missing").string() + "\n"));
}

TEST(realpath, realpath_existing_file_with_trailing_separator_fails) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {L"file.txt/"});
  auto r = p.run();

  // [GNU] A trailing separator on an existing non-directory is ENOTDIR.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("Not a directory") != std::string::npos);
}

namespace {

// #1231: the MSYS single-letter drive spelling of a native path
// ("D:\repo\x" -> "/d/repo/x"): lowercase drive letter, forward separators.
auto drive_dialect_spelling(const std::filesystem::path& native)
    -> std::wstring {
  const std::wstring text = native.wstring();
  wchar_t drive = text[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  std::wstring dialect;
  dialect.push_back(L'/');
  dialect.push_back(drive);
  for (size_t i = 2; i < text.size(); ++i) {
    dialect.push_back(text[i] == L'\\' ? L'/' : text[i]);
  }
  return dialect;
}

// The dialect fold runs before the shell-tree mapping, so the expectations
// below hold with the shell namespace unconfigured too; pin that down.
void clear_shell_root_env(Pipeline& pipeline) {
  for (const auto* name :
       {L"__RUBASH_SHELL_ROOT", L"NIU_ROOT", L"WINUXSH_ROOT", L"RUBASH_ROOT"}) {
    pipeline.set_env(name, L"");
  }
}

// #1246: the /cygdrive/X spelling of a native path ("D:\repo\x" ->
// "/cygdrive/d/repo/x").
auto cygdrive_prefix_spelling(const std::filesystem::path& native)
    -> std::wstring {
  return L"/cygdrive" + drive_dialect_spelling(native);
}

// #1246: the /mnt/X spelling of a native path ("D:\repo\x" ->
// "/mnt/d/repo/x").
auto mnt_prefix_spelling(const std::filesystem::path& native) -> std::wstring {
  return L"/mnt" + drive_dialect_spelling(native);
}

}  // namespace

TEST(realpath, realpath_drive_dialect_resolves_from_foreign_cwd) {
  TempDir dir;
  TempDir cwd;
  dir.mkdir("prog");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"realpath.exe", {drive_dialect_spelling(dir.path) + L"/prog"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath drive dialect output", r.stdout_text);

  // Before #1231 the operand had no root name in the std::filesystem sense,
  // so the resolver walked it relative to the cwd and reported ENOENT for a
  // directory that exists on the named drive.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                 normalize_path_text((dir.path / "prog").string() + "\n"));
}

TEST(realpath, realpath_missing_mode_keeps_drive_dialect_native_spelling) {
  TempDir dir;
  TempDir cwd;
  dir.mkdir("prog");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"realpath.exe",
        {L"-m", drive_dialect_spelling(dir.path) + L"/prog/absent.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath -m drive dialect output", r.stdout_text);

  // -m prints the folded native spelling, never a cwd-stranded walk.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      normalize_path_text(r.stdout_text),
      normalize_path_text((dir.path / "prog" / "absent.txt").string() + "\n"));
}

TEST(realpath, realpath_bare_drive_dialect_is_drive_root) {
  TempDir cwd;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"realpath.exe",
        {L"-m", drive_dialect_spelling(cwd.path).substr(0, 2)});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath bare drive dialect output", r.stdout_text);

  // "/x" names the root of the drive, not <cwd>\x.
  EXPECT_EQ(r.exit_code, 0);
  std::string expected(
      1, static_cast<char>(
             std::toupper(static_cast<unsigned char>(cwd.path.string()[0]))));
  expected += ":\\\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(realpath, realpath_multi_letter_first_segment_is_not_a_drive) {
  TempDir cwd;

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"realpath.exe", {L"-m", L"/nodrive"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath multi-letter first segment output", r.stdout_text);

  // The recognition rule is exactly one drive letter: "nodrive" is an
  // ordinary POSIX word, so the operand keeps resolving relative to the cwd.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                 normalize_path_text((cwd.path / "nodrive").string() + "\n"));
}

// ===== #1246 regression: /cygdrive/X and /mnt/X prefixes in the resolver =====
//
// canonicalize_path_w folded only the single-letter drive form (#1231), so
// realpath/readlink -f stranded "/cygdrive/e/..." on the cwd (ENOENT in the
// default mode, a "<cwd-drive>:\cygdrive\e\..." spelling under -m) while
// stat/test -f folded the same spelling at the argv boundary — the two
// commands disagreed on one operand ("test -f said yes, realpath said no",
// the #850 family). The resolver now runs the same cygdrive (#1145) and
// /mnt (#1172) prefix folds before the single-letter rule, in the same
// order as normalize_api_operand_w.

TEST(realpath, realpath_cygdrive_and_mnt_prefixes_resolve_from_foreign_cwd) {
  TempDir dir;
  TempDir cwd;
  dir.mkdir("prog");

  Pipeline p;
  p.set_cwd(cwd.wpath());
  clear_shell_root_env(p);
  p.add(L"realpath.exe",
        {L"-m", cygdrive_prefix_spelling(dir.path) + L"/prog"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("realpath -m cygdrive prefix output", r.stdout_text);

  // Before #1246 -m printed the stranded "<cwd-drive>:\cygdrive\e\..." walk.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                 normalize_path_text((dir.path / "prog").string() + "\n"));

  Pipeline m;
  m.set_cwd(cwd.wpath());
  clear_shell_root_env(m);
  m.add(L"realpath.exe", {mnt_prefix_spelling(dir.path) + L"/prog"});
  auto rm = m.run();

  TEST_LOG_EXIT_CODE(rm);
  TEST_LOG("realpath mnt prefix output", rm.stdout_text);

  // Before #1246 the default mode reported ENOENT for the existing directory.
  EXPECT_EQ(rm.exit_code, 0);
  EXPECT_EQ_TEXT(normalize_path_text(rm.stdout_text),
                 normalize_path_text((dir.path / "prog").string() + "\n"));
}

TEST(realpath, realpath_cygdrive_prefix_resolves_identically_with_shell_root) {
  TempDir dir;
  TempDir cwd;
  TempDir root;
  dir.mkdir("prog");

  for (const bool shell_root : {true, false}) {
    Pipeline p;
    p.set_cwd(cwd.wpath());
    clear_shell_root_env(p);
    if (shell_root) {
      // A configured root must not capture the prefix spelling: like the
      // argv boundary, a folded drive operand is native-dialect input.
      p.set_env(L"__RUBASH_SHELL_ROOT", root.wpath());
    }
    p.add(L"realpath.exe",
          {L"-m", cygdrive_prefix_spelling(dir.path) + L"/prog"});
    auto r = p.run();

    TEST_LOG_EXIT_CODE(r);
    TEST_LOG("realpath cygdrive prefix, shell root=" +
                 std::string(shell_root ? "set" : "cleared"),
             r.stdout_text);

    // Both states print the native spelling; before #1246 the configured
    // state stranded the operand under <root>\cygdrive\e\... instead.
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ_TEXT(normalize_path_text(r.stdout_text),
                   normalize_path_text((dir.path / "prog").string() + "\n"));
  }
}

TEST(realpath, issue_1283_drive_colon_operand_resolves_carrier_file) {
  TempDir tmp;
  const wchar_t drive = tmp.wpath()[0];
  const std::wstring carrier =
      std::wstring(1, drive) + L'\uF03A' + L"wux.txt";
  temp_dir_detail::write_all(tmp.path / carrier, "hello\n", 6);
  const std::wstring operand = std::wstring(1, drive) + L":wux.txt";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"realpath.exe", {operand});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("wux.txt"), std::string::npos);
}
