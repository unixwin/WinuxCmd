#include "framework/winuxtest.h"

TEST(regtool, get_reads_string_value) {
  Pipeline p;
  p.add(L"regtool.exe",
        {L"get", L"HKLM/SOFTWARE/Microsoft/Cryptography", L"MachineGuid"});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  ASSERT_FALSE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stdout_text, "\n");
}

TEST(regtool, list_prints_value_names) {
  Pipeline p;
  p.add(L"regtool.exe", {L"list", L"HKLM/SOFTWARE/Microsoft/Cryptography"});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stdout_text, "MachineGuid\n");
}

TEST(regtool, accepts_backslash_separated_paths) {
  Pipeline p;
  p.add(L"regtool.exe",
        {L"get", L"HKLM\\SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(regtool, rejects_unknown_root_and_action) {
  Pipeline bad_root;
  bad_root.add(L"regtool.exe", {L"list", L"NOPE/Software"});
  auto bad_root_result = bad_root.run();
  EXPECT_EQ(bad_root_result.exit_code, 1);
  EXPECT_CONTAINS(bad_root_result.stderr_text, "unknown registry root");

  Pipeline bad_action;
  bad_action.add(L"regtool.exe", {L"wat", L"HKCU/Software"});
  auto bad_action_result = bad_action.run();
  EXPECT_EQ(bad_action_result.exit_code, 1);
  EXPECT_CONTAINS(bad_action_result.stderr_text, "unknown action");
}

TEST(regtool, set_and_remove_validate_arguments_before_writing) {
  Pipeline bad_set;
  bad_set.add(L"regtool.exe", {L"set", L"HKCU/Software/WinuxCmdTests"});
  auto bad_set_result = bad_set.run();
  EXPECT_EQ(bad_set_result.exit_code, 1);
  EXPECT_CONTAINS(bad_set_result.stderr_text, "usage: regtool set");

  Pipeline bad_remove;
  bad_remove.add(L"regtool.exe",
                 {L"remove", L"HKCU/Software/WinuxCmdTests", L"A", L"B"});
  auto bad_remove_result = bad_remove.run();
  EXPECT_EQ(bad_remove_result.exit_code, 1);
  EXPECT_CONTAINS(bad_remove_result.stderr_text, "usage: regtool remove");
}
