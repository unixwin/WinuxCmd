// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

namespace {

auto slash_drive_path(const std::filesystem::path& path) -> std::wstring {
  auto text = path.generic_wstring();
  if (text.size() >= 3 && text[1] == L':' &&
      (text[2] == L'/' || text[2] == L'\\') &&
      ((text[0] >= L'A' && text[0] <= L'Z') ||
       (text[0] >= L'a' && text[0] <= L'z'))) {
    std::wstring out;
    out.reserve(text.size());
    out.push_back(L'/');
    wchar_t drive = text[0];
    if (drive >= L'A' && drive <= L'Z') {
      drive = static_cast<wchar_t>(drive - L'A' + L'a');
    }
    out.push_back(drive);
    out.append(text.substr(2));
    return out;
  }
  return text;
}

}  // namespace

TEST(mktemp, mktemp_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Remove trailing newline
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_FALSE(filename.starts_with("/c/"));
  EXPECT_TRUE(filename.size() >= 3 && filename[1] == 58 && filename[2] == 47);
  EXPECT_EQ(filename.find("~"), std::string::npos);
  auto created_path = std::filesystem::u8path(filename);
  EXPECT_TRUE(std::filesystem::exists(created_path));
  std::filesystem::remove(created_path);
}

TEST(mktemp, mktemp_default_template_uses_tmpdir_env_as_native_path) {
  TempDir tmp;
  auto out_dir = tmp.path / "out";
  std::filesystem::create_directory(out_dir);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"TMPDIR", slash_drive_path(out_dir));
  p.add(L"mktemp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == 10) {
    created.pop_back();
  }

  EXPECT_FALSE(created.starts_with("/c/"));
  EXPECT_TRUE(created.size() >= 3 && created[1] == 58 && created[2] == 47);
  EXPECT_EQ(created.find("~"), std::string::npos);
  auto created_path = std::filesystem::u8path(created);
  EXPECT_TRUE(std::filesystem::exists(created_path));
  EXPECT_TRUE(std::filesystem::equivalent(created_path.parent_path(), out_dir));
}

TEST(mktemp, mktemp_custom_prefix) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"test_XXXXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.find("test_") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_template_replaces_x_run_before_existing_suffix) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"file-XXXX.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.starts_with("file-"));
  EXPECT_TRUE(filename.ends_with(".txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_dry_run) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"-u", L"test_XXXXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // With -u, file should not actually be created
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_FALSE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_dry_run_randomizes_across_processes) {
  TempDir tmp;

  auto run_once = [&tmp]() {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"mktemp.exe", {L"-u", L"probe-XXXXXXXXXX"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    std::string name = r.stdout_text;
    if (!name.empty() && name.back() == 10) {
      name.pop_back();
    }
    return name;
  };

  auto first = run_once();
  bool saw_different = false;
  for (int i = 0; i < 5; ++i) {
    if (run_once() != first) {
      saw_different = true;
      break;
    }
  }

  EXPECT_TRUE(saw_different);
}

TEST(mktemp, mktemp_suffix_appends_to_template) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"--suffix=.txt", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string filename = r.stdout_text;
  if (!filename.empty() && filename.back() == '\n') {
    filename.pop_back();
  }
  EXPECT_TRUE(filename.starts_with("file-"));
  EXPECT_TRUE(filename.ends_with(".txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / filename));
}

TEST(mktemp, mktemp_suffix_rejects_directory_separator) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"--suffix=dir\\bad", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "");
  EXPECT_TRUE(
      r.stderr_text.find(
          "mktemp: invalid suffix 'dir\\bad', contains directory separator") !=
      std::string::npos);
}

TEST(mktemp, mktemp_tmpdir_prints_path_not_just_basename) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "out");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"-p", L"out", L"file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == '\n') {
    created.pop_back();
  }
  EXPECT_TRUE(created.starts_with("out\\") || created.starts_with("out/"));
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / std::filesystem::u8path(created)));
}

TEST(mktemp, mktemp_template_with_directory_prints_relative_path) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "nested");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {L"nested\\file-XXXX"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == '\n') {
    created.pop_back();
  }
  EXPECT_TRUE(created.starts_with("nested\\") ||
              created.starts_with("nested/"));
  EXPECT_TRUE(
      std::filesystem::exists(tmp.path / std::filesystem::u8path(created)));
}

TEST(mktemp, mktemp_slash_drive_template_outputs_native_path) {
  TempDir tmp;
  auto out_dir = tmp.path / "out";
  std::filesystem::create_directory(out_dir);
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mktemp.exe", {slash_drive_path(out_dir) + L"/file-XXXX"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string created = r.stdout_text;
  if (!created.empty() && created.back() == '\n') {
    created.pop_back();
  }
  EXPECT_FALSE(created.starts_with("/c/"));
  EXPECT_TRUE(created.size() >= 3 && created[1] == ':' && created[2] == '/');
  EXPECT_TRUE(created.find("/file-") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::u8path(created)));
}
