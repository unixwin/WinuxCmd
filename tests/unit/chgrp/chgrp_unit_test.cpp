/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(chgrp, chgrp_no_args_shows_error) {
  Pipeline p;
  p.add(L"chgrp.exe", {});
  auto r = p.run();

  // Should fail with missing operand error
  EXPECT_NE(r.exit_code, 0);
}

TEST(chgrp, chgrp_missing_group) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"nonexistent"});
  auto r = p.run();

  // Should fail with missing file operand
  EXPECT_NE(r.exit_code, 0);
}

TEST(chgrp, chgrp_nonexistent_file) {
  Pipeline p;
  p.add(L"chgrp.exe", {L"Users", L"nonexistent_file_xyz"});
  auto r = p.run();

  // Should fail with no such file
  EXPECT_NE(r.exit_code, 0);
}

TEST(chgrp, chgrp_nonexistent_group) {
  TempDir tmp;
  std::ofstream(tmp.path / "test.txt") << "hello";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chgrp.exe", {L"nonexistent_group_xyz", L"test.txt"});
  auto r = p.run();

  // Should fail with invalid group
  EXPECT_NE(r.exit_code, 0);
}
