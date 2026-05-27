/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dircolors, dircolors_default_bourne) {
  Pipeline p;
  p.add(L"dircolors.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output Bourne shell code with LS_COLORS
  EXPECT_TRUE(r.stdout_text.find("LS_COLORS") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("export LS_COLORS") != std::string::npos);
}

TEST(dircolors, dircolors_csh) {
  Pipeline p;
  p.add(L"dircolors.exe", {L"-c"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output C shell code with setenv
  EXPECT_TRUE(r.stdout_text.find("setenv LS_COLORS") != std::string::npos);
}

TEST(dircolors, dircolors_print_database) {
  Pipeline p;
  p.add(L"dircolors.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output the default color database
  EXPECT_TRUE(r.stdout_text.find("Configuration file for dircolors") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("di 01;34") != std::string::npos);
}

TEST(dircolors, dircolors_print_ls_colors) {
  Pipeline p;
  p.add(L"dircolors.exe", {L"--print-ls-colors"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should output raw color codes
  EXPECT_TRUE(r.stdout_text.find("di=01;34") != std::string::npos);
}
