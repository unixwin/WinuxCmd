#include "framework/winuxtest.h"

static std::wstring winuxsh_drive_path(const std::filesystem::path& path) {
  auto text = path.generic_wstring();
  if (text.size() >= 3 && text[1] == L':' && text[2] == L'/') {
    wchar_t drive = text[0];
    if (drive >= L'A' && drive <= L'Z')
      drive = static_cast<wchar_t>(drive - L'A' + L'a');
    return L"/" + std::wstring(1, drive) + text.substr(2);
  }
  return text;
}

TEST(which, which_finds_first_match) {
  TempDir tmp;
  tmp.write("tool.exe", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"tool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("tool.exe") != std::string::npos);
}

TEST(which, which_all_lists_multiple) {
  TempDir tmp;
  tmp.write("a.exe", "");
  tmp.write("a.cmd", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"-a", L"a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.exe") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.cmd") != std::string::npos);
}

TEST(which, which_prefers_pathext_wrapper_before_extensionless_script) {
  TempDir tmp;
  tmp.write("code", "#!/usr/bin/env sh\n");
  tmp.write("code.cmd", "@echo off\r\n");

  Pipeline first;
  first.set_cwd(tmp.wpath());
  first.set_env(L"PATH", tmp.wpath());
  first.set_env(L"PATHEXT", L".EXE;.PS1");
  first.add(L"which.exe", {L"code"});
  auto first_result = first.run();

  EXPECT_EQ(first_result.exit_code, 0);
  EXPECT_TRUE(first_result.stdout_text.find("code.cmd\n") != std::string::npos);
  EXPECT_TRUE(first_result.stdout_text.find("/code\n") == std::string::npos);

  Pipeline all;
  all.set_cwd(tmp.wpath());
  all.set_env(L"PATH", tmp.wpath());
  all.set_env(L"PATHEXT", L".EXE;.PS1");
  all.add(L"which.exe", {L"-a", L"code"});
  auto all_result = all.run();

  EXPECT_EQ(all_result.exit_code, 0);
  auto wrapper_pos = all_result.stdout_text.find("code.cmd");
  auto script_pos = all_result.stdout_text.find("/code\n");
  EXPECT_TRUE(wrapper_pos != std::string::npos);
  EXPECT_TRUE(script_pos != std::string::npos);
  EXPECT_TRUE(wrapper_pos < script_pos);
}

TEST(which, which_prefers_ps1_before_extensionless_when_pathext_omits_ps1) {
  TempDir tmp;
  tmp.write("tool", "#!/usr/bin/env sh\n");
  tmp.write("tool.ps1", "Write-Output tool\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.set_env(L"PATHEXT", L".EXE;.BAT;.CMD");
  p.add(L"which.exe", {L"tool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("tool.ps1\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("/tool\n") == std::string::npos);
}

TEST(which, which_missing_returns_nonzero) {
  TempDir tmp;
  tmp.write("present.exe", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"absent"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(which, which_ignores_directory_named_like_executable) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "tool.exe");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"tool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(which, which_skip_dot_skips_current_directory) {
  TempDir tmp;
  tmp.write("dottool.exe", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", L".");
  p.add(L"which.exe", {L"--skip-dot", L"dottool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(which, which_show_dot_prints_dot_relative_result) {
  TempDir tmp;
  tmp.write("dottool.exe", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", L".");
  p.add(L"which.exe", {L"--show-dot", L"dottool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "./dottool.exe\n");
}

TEST(which, which_skip_tilde_skips_home_entry_and_home_result) {
  TempDir home;
  TempDir work;
  home.write("hometool.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"HOME", home.wpath());
  p.set_env(L"PATH", home.wpath());
  p.add(L"which.exe", {L"--skip-tilde", L"hometool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(which, which_show_tilde_prints_home_relative_result) {
  TempDir home;
  TempDir work;
  home.write("hometool.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"HOME", home.wpath());
  p.set_env(L"PATH", home.wpath());
  p.add(L"which.exe", {L"--show-tilde", L"hometool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "~/hometool.exe\n");
}

TEST(which, which_expands_tilde_path_entries) {
  TempDir home;
  TempDir work;
  home.write("bin/tilde-tool.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"HOME", home.wpath());
  p.set_env(L"PATH", L"~/bin");
  p.add(L"which.exe", {L"--show-tilde", L"tilde-tool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "~/bin/tilde-tool.exe\n");
}

TEST(which, which_skip_tilde_skips_tilde_path_entries) {
  TempDir home;
  TempDir work;
  home.write("bin/tilde-tool.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"HOME", home.wpath());
  p.set_env(L"PATH", L"~/bin");
  p.add(L"which.exe", {L"--skip-tilde", L"tilde-tool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}
TEST(which, which_accepts_winuxsh_drive_style_path_entries) {
  TempDir home;
  TempDir work;
  home.write("shelltool.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"PATH", winuxsh_drive_path(home.path));
  p.add(L"which.exe", {L"shelltool"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("shelltool.exe") != std::string::npos);
}

TEST(which, which_show_tilde_accepts_winuxsh_home_path) {
  TempDir home;
  TempDir work;
  home.write("shellhome.exe", "");

  Pipeline p;
  p.set_cwd(work.wpath());
  p.set_env(L"HOME", winuxsh_drive_path(home.path));
  p.set_env(L"PATH", winuxsh_drive_path(home.path));
  p.add(L"which.exe", {L"--show-tilde", L"shellhome"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "~/shellhome.exe\n");
}

TEST(which, which_new_selection_options) {
  TempDir tmp;
  tmp.write("tool.exe", "");
  for (const auto& option : {L"--silent", L"--quiet"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.set_env(L"PATH", tmp.wpath());
    p.add(L"which.exe", {option, L"tool"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.empty());
  }
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"--skip-alias", L"--skip-functions", L"tool"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("tool.exe"), std::string::npos);
}

TEST(which, which_tty_only_suppresses_pipe_output) {
  TempDir tmp;
  tmp.write("tool.exe", "");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"PATH", tmp.wpath());
  p.add(L"which.exe", {L"--tty-only", L"tool"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}
