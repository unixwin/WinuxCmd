/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dir, dir_no_args_lists_current) {
  Pipeline p;
  p.add(L"dir.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should list files in column format
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(dir, dir_lists_files) {
  TempDir tmp;
  std::ofstream(tmp.path / "a.txt") << "hello";
  std::ofstream(tmp.path / "b.txt") << "world";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dir.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(dir, dir_long_format) {
  TempDir tmp;
  std::ofstream(tmp.path / "plain.txt") << "hello";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dir.exe", {L"-l"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("plain.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("total ") != std::string::npos);
}

TEST(dir, dir_show_hidden) {
  TempDir tmp;
  std::ofstream(tmp.path / ".hidden.txt") << "secret";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dir.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find(".hidden.txt") != std::string::npos);
}

TEST(dir, dir_recursive) {
  Pipeline p;
  p.add(L"dir.exe", {L"-R"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(dir, dir_preserves_single_argument_with_spaces) {
  TempDir tmp;
  auto spaced_dir = tmp.path / "test data";
  std::filesystem::create_directory(spaced_dir);
  std::ofstream(spaced_dir / "alpha.txt") << "x";

  Pipeline p;
  p.add(L"dir.exe", {spaced_dir.wstring()});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("alpha.txt") != std::string::npos);
}

TEST(dir, dir_forwards_directory_option_to_ls) {
  TempDir tmp;
  auto spaced_dir = tmp.path / "test data";
  std::filesystem::create_directory(spaced_dir);
  std::ofstream(spaced_dir / "alpha.txt") << "x";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dir.exe", {L"-d", L"test data"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_TRUE(r.stdout_text.find("test data") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("alpha.txt") == std::string::npos);
}
