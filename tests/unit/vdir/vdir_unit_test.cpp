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
  Pipeline p;
  p.add(L"vdir.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(vdir, vdir_recursive) {
  Pipeline p;
  p.add(L"vdir.exe", {L"-R"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
