/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(vdir, vdir_no_args_lists_long) {
  Pipeline p;
  p.add(L"vdir.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should list files in long format
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(vdir, vdir_lists_files) {
  TempDir tmp;
  std::ofstream(tmp.path / "test.txt") << "hello";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"vdir.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test.txt") != std::string::npos);
}

TEST(vdir, vdir_show_hidden) {
  TempDir tmp;
  std::ofstream(tmp.path / ".hidden.txt") << "secret";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"vdir.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(vdir, vdir_recursive) {
  Pipeline p;
  p.add(L"vdir.exe", {L"-R"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(vdir, vdir_preserves_single_argument_with_spaces) {
  TempDir tmp;
  auto spaced_dir = tmp.path / "test data";
  std::filesystem::create_directory(spaced_dir);
  std::ofstream(spaced_dir / "beta.txt") << "x";

  Pipeline p;
  p.add(L"vdir.exe", {spaced_dir.wstring()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("beta.txt") != std::string::npos);
}

TEST(vdir, vdir_forwards_directory_option_to_ls) {
  TempDir tmp;
  auto spaced_dir = tmp.path / "test data";
  std::filesystem::create_directory(spaced_dir);
  std::ofstream(spaced_dir / "beta.txt") << "x";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"vdir.exe", {L"-d", L"test data"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("test data") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("beta.txt") == std::string::npos);
}
