#include "framework/winuxtest.h"

TEST(chown, chown_reference_form_accepts_files_without_owner_operand) {
  TempDir tmp;
  tmp.write("reference.txt", "ref");
  tmp.write("target.txt", "target");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chown.exe", {L"--reference=reference.txt", L"target.txt"});

  TEST_LOG_CMD_LIST("chown.exe", L"--reference=reference.txt", L"target.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chown stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}
