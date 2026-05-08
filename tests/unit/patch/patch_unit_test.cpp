/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(patch, patch_basic) {
  TempDir tmp;
  tmp.write("file.txt", "hello\n");
  std::string patch_data =
      "--- a/file.txt\n+++ b/file.txt\n@@ -1,1 +1,1 @@\n-hello\n+world\n";

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin(patch_data);
  p.add(L"patch.exe", {});

  TEST_LOG_CMD_LIST("patch.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("patch stdout", r.stdout_text);
  TEST_LOG("patch stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
}
