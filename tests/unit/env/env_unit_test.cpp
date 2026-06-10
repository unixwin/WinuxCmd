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

auto csharp_compiler() -> std::wstring {
  wchar_t buffer[MAX_PATH]{};
  UINT len = GetWindowsDirectoryW(buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return L"csc.exe";
  std::wstring path(buffer, len);
  path += L"\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe";
  return path;
}

auto write_argv0_helper(const TempDir& tmp) -> std::filesystem::path {
  auto source = tmp.path / "argv0check.cs";
  auto exe = tmp.path / "argv0check.exe";
  std::ofstream out(source, std::ios::binary);
  out << "using System;\n"
         "class P {\n"
         "  static void Main(string[] args) {\n"
         "    Console.WriteLine(Environment.GetCommandLineArgs()[0]);\n"
         "  }\n"
         "}\n";
  out.close();

  Pipeline build;
  build.set_cwd(tmp.wpath());
  build.add(csharp_compiler(), {L"/nologo", L"/out:argv0check.exe",
                                L"argv0check.cs"});
  auto result = build.run();
  if (result.exit_code != 0 || !std::filesystem::exists(exe)) {
    return {};
  }
  return exe;
}

auto has_env_line(std::string_view output, std::string_view key,
                  std::string_view value) -> bool {
  std::string needle;
  needle.reserve(key.size() + value.size() + 2);
  needle.append(key);
  needle.push_back('=');
  needle.append(value);

  if (output == needle) return true;
  if (output.find(needle + "\n") == 0) return true;
  if (output.find("\n" + needle + "\n") != std::string::npos) return true;
  if (output.size() > needle.size() &&
      output.ends_with(std::string("\n") + needle)) {
    return true;
  }
  return false;
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

TEST(env, env_file_loads_simple_assignments_for_printing) {
  TempDir tmp;
  tmp.write("vars.env", "A=1\n#comment\nC = spaced\nEMPTY=\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-f", L"vars.env"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(has_env_line(r.stdout_text, "A", "1"));
  EXPECT_TRUE(has_env_line(r.stdout_text, "C", "spaced"));
  EXPECT_TRUE(has_env_line(r.stdout_text, "EMPTY", ""));
}

TEST(env, env_file_applies_before_unset_and_cli_assignments) {
  TempDir tmp;
  tmp.write("vars.env", "A=1\nB=two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-f", L"vars.env", L"-u", L"A", L"B=3"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(has_env_line(r.stdout_text, "A", "1"));
  EXPECT_TRUE(has_env_line(r.stdout_text, "B", "3"));
  EXPECT_FALSE(has_env_line(r.stdout_text, "B", "two"));
}

TEST(env, env_file_supplies_command_environment) {
  TempDir tmp;
  tmp.write("vars.env", "A=1\nB=two\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"-f", L"vars.env", system_cmd(), L"/C", L"echo", L"%A% %B%"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1 two") != std::string::npos);
}

TEST(env, env_file_missing_path_reports_read_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-f", L"missing.env"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "env: cannot read env file 'missing.env': No such file or "
                 "directory\n");
}

TEST(env, env_file_directory_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"--file", L"indir"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "env: cannot read env file 'indir': Is a directory\n");
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

TEST(env, env_null_with_command_reports_usage_error) {
  Pipeline p;
  p.add(L"env.exe", {L"--null", system_cmd(), L"/C", L"echo", L"ok"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "env: cannot specify --null (-0) with command\n");
}

TEST(env, env_split_string_rejects_unterminated_quote) {
  Pipeline p;
  p.add(L"env.exe", {L"-S", L"cmd /C \"echo"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stderr_text.find("no terminating quote in -S string") !=
              std::string::npos);
}

TEST(env, env_split_string_treats_newlines_as_separators) {
  Pipeline p;
  p.add(L"env.exe", {L"-S", L"A=1\nB=2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("A=1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("B=2") != std::string::npos);
}

TEST(env, env_split_string_backslash_underscore_splits_arguments) {
  Pipeline p;
  p.add(L"env.exe", {L"-S", L"A=1\\_B=2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("A=1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("B=2") != std::string::npos);
}

TEST(env, env_split_string_double_quoted_backslash_underscore_becomes_space) {
  Pipeline p;
  p.add(L"env.exe", {L"-S", L"A=\"left\\_right\""});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("A=left right") != std::string::npos);
}

TEST(env, env_split_string_backslash_c_ignores_remaining_text) {
  Pipeline p;
  p.add(L"env.exe", {L"-S", L"A=1\\c B=2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("A=1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("B=2") == std::string::npos);
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

TEST(env, env_chdir_requires_command) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "work");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-C", L"work"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stderr_text.find("must specify command with --chdir") !=
              std::string::npos);
}

TEST(env, env_chdir_last_occurrence_wins_across_aliases) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "work1");
  std::filesystem::create_directories(tmp.path / "work2");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"--chdir", L"work1", L"-C", L"work2", system_cmd(), L"/C", L"cd"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find((tmp.path / "work2").string()) !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find((tmp.path / "work1").string()) ==
              std::string::npos);
}

TEST(env, env_short_debug_flag_emits_diagnostics) {
  Pipeline p;
  p.add(L"env.exe", {L"-v", system_cmd(), L"/C", L"echo", L"ok"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("ok") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("executing: ") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("arg[0]= ") != std::string::npos);
}

TEST(env, env_debug_shows_overridden_argv0) {
  TempDir tmp;
  auto helper = write_argv0_helper(tmp);
  EXPECT_TRUE(!helper.empty());
  if (helper.empty()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"-v", L"--argv0", L"debug-name", L"argv0check.exe"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("argv0:     debug-name") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("arg[0]= debug-name") !=
              std::string::npos);
}

TEST(env, env_double_debug_prints_input_args) {
  Pipeline p;
  p.add(L"env.exe", {L"-vv", system_cmd(), L"/C", L"echo", L"ok"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("input args:") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("arg[0]: -vv") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("executing: ") != std::string::npos);
}

TEST(env, env_mixed_debug_flags_accumulate_level) {
  Pipeline p;
  p.add(L"env.exe",
        {L"-v", L"--debug", system_cmd(), L"/C", L"echo", L"ok"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("input args:") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("arg[1]: --debug") != std::string::npos);
}

TEST(env, env_argv0_overrides_child_command_line_argv0) {
  TempDir tmp;
  auto helper = write_argv0_helper(tmp);
  EXPECT_TRUE(!helper.empty());
  if (helper.empty()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"--argv0", L"hijacked-name", L"argv0check.exe"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "hijacked-name\r\n");
}

TEST(env, env_argv0_last_occurrence_wins) {
  TempDir tmp;
  auto helper = write_argv0_helper(tmp);
  EXPECT_TRUE(!helper.empty());
  if (helper.empty()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"-a", L"first-name", L"--argv0", L"final-name", L"argv0check.exe"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "final-name\r\n");
}

TEST(env, env_argv0_last_occurrence_wins_across_aliases) {
  TempDir tmp;
  auto helper = write_argv0_helper(tmp);
  EXPECT_TRUE(!helper.empty());
  if (helper.empty()) return;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"--argv0", L"first-name", L"-a", L"final-name", L"argv0check.exe"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "final-name\r\n");
}

TEST(env, env_split_string_last_occurrence_wins_across_aliases) {
  Pipeline p;
  p.set_env(L"A", L"old");
  p.add(L"env.exe",
        {L"--split-string", L"A=first", L"-S", L"A=second"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("A=second") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("A=first") == std::string::npos);
}

TEST(env, env_missing_command_reports_gnu_style_error_and_127) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"definitely-not-a-command"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("env missing command stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "env: failed to run command 'definitely-not-a-command': No such file or "
      "directory\n");
}

TEST(env, env_ignore_environment_missing_command_reports_gnu_style_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L"-i", L"definitely-not-a-command"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "env: failed to run command 'definitely-not-a-command': No such file or "
      "directory\n");
}

TEST(env, env_non_executable_command_returns_126) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "not-executable");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe", {L".\\not-executable"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 126);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "env: failed to run command '.\\not-executable': Permission denied\n");
}

TEST(env, env_argv0_non_executable_command_returns_126) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "not-executable");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"env.exe",
        {L"--argv0", L"fake-name", L".\\not-executable"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 126);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "env: failed to run command '.\\not-executable': Permission denied\n");
}

TEST(env, env_invalid_option_returns_125_with_help_hint) {
  Pipeline p;
  p.add(L"env.exe", {L"--definitely-invalid"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "env: unrecognized option '--definitely-invalid'\n"
      "Try 'env --help' for more information.\n");
}
