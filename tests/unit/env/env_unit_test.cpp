#include "framework/winuxtest.h"

namespace {

auto system_cmd() -> std::wstring {
  wchar_t buffer[MAX_PATH]{};
  UINT len = GetSystemDirectoryW(buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return L"cmd.exe";
  std::wstring path(buffer, len);
  path += L"\\cmd.exe";
  return path;
}

}  // namespace

TEST(env, env_lists_current) {
  Pipeline p;
  p.set_env(L"FOO", L"BAR");
  p.add(L"env.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("FOO=BAR") != std::string::npos);
}

TEST(env, env_ignore_environment_and_set) {
  Pipeline p;
  p.set_env(L"SHOULD_NOT", L"SEE");
  p.add(L"env.exe", {L"-i", L"X=1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("X=1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("SHOULD_NOT") == std::string::npos);
}

TEST(env, env_unset_variable) {
  Pipeline p;
  p.set_env(L"KEEP", L"1");
  p.set_env(L"DROP", L"1");
  p.add(L"env.exe", {L"-u", L"DROP", L"KEEP=2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("DROP=") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KEEP=2") != std::string::npos);
}

TEST(env, env_unset_can_be_repeated) {
  Pipeline p;
  p.set_env(L"DROP1", L"1");
  p.set_env(L"DROP2", L"1");
  p.set_env(L"KEEP", L"1");
  p.add(L"env.exe", {L"-u", L"DROP1", L"--unset", L"DROP2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("DROP1=") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("DROP2=") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KEEP=1") != std::string::npos);
}

TEST(env, env_runs_command_with_assignment) {
  Pipeline p;
  p.add(L"env.exe", {L"FOO=BAR", system_cmd(), L"/C", L"echo", L"%FOO%"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("BAR") != std::string::npos);
}

TEST(env, env_runs_command_with_empty_environment) {
  Pipeline p;
  p.set_env(L"SHOULD_NOT", L"SEE");
  p.add(L"env.exe",
        {L"-i", L"FOO=BAR", system_cmd(), L"/C", L"echo", L"%FOO%"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("BAR") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("SHOULD_NOT") == std::string::npos);
}

TEST(env, env_chdir_runs_command_in_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "work");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-C", L"work", system_cmd(), L"/C", L"cd"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find((tmp.path / "work").string()) !=
              std::string::npos);
}

TEST(env, env_chdir_rejects_missing_directory) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"--chdir", L"missing", system_cmd(), L"/C", L"cd"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stderr_text.find("cannot change directory") !=
              std::string::npos);
}
