/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(xxd, default_file_format) {
  TempDir tmp;
  tmp.write("input.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "00000000: 6865 6c6c 6f                             hello\n");
}

TEST(xxd, custom_columns) {
  TempDir tmp;
  tmp.write("input.txt", "hell");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-c", L"4", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "00000000: 6865 6c6c  hell\n");
}

TEST(xxd, offset_adds_to_default_addresses) {
  TempDir tmp;
  tmp.write("input.txt", "abcdefgh");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-o", L"16", L"-c", L"4", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "00000010: 6162 6364  abcd\n"
                 "00000014: 6566 6768  efgh\n");
}

TEST(xxd, plain_columns) {
  TempDir tmp;
  tmp.write("input.txt", "abcdefgh");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-p", L"-c", L"4", L"input.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "61626364\n65666768\n");
}

TEST(xxd, len_limits_input_for_short_and_long_options) {
  TempDir tmp;
  tmp.write("input.txt", "abcdef");

  Pipeline short_option;
  short_option.set_cwd(tmp.wpath());
  short_option.add(L"xxd.exe", {L"-p", L"-l", L"2", L"input.txt"});

  Pipeline long_option;
  long_option.set_cwd(tmp.wpath());
  long_option.add(L"xxd.exe", {L"-p", L"--len", L"3", L"input.txt"});

  auto short_result = short_option.run();
  auto long_result = long_option.run();

  EXPECT_EQ(short_result.exit_code, 0);
  EXPECT_EQ_TEXT(short_result.stdout_text, "6162\n");
  EXPECT_EQ(long_result.exit_code, 0);
  EXPECT_EQ_TEXT(long_result.stdout_text, "616263\n");
}

TEST(xxd, seek_starts_input_for_short_and_long_options) {
  TempDir tmp;
  tmp.write("input.txt", "abcdef");

  Pipeline short_option;
  short_option.set_cwd(tmp.wpath());
  short_option.add(L"xxd.exe", {L"-p", L"-s", L"2", L"input.txt"});

  Pipeline long_option;
  long_option.set_cwd(tmp.wpath());
  long_option.add(L"xxd.exe", {L"-p", L"--seek", L"3", L"input.txt"});

  auto short_result = short_option.run();
  auto long_result = long_option.run();

  EXPECT_EQ(short_result.exit_code, 0);
  EXPECT_EQ_TEXT(short_result.stdout_text, "63646566\n");
  EXPECT_EQ(long_result.exit_code, 0);
  EXPECT_EQ_TEXT(long_result.stdout_text, "646566\n");
}

TEST(xxd, plain_uppercase) {
  TempDir tmp;
  tmp.write_bytes("input.bin", {'\xAB', '\xCD', '\xEF'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-p", L"-u", L"input.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ABCDEF\n");
}

TEST(xxd, reverse_plain) {
  TempDir tmp;
  tmp.write("hex.txt", "68656c6c6f");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-r", L"-p", L"hex.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "hello");
}

TEST(xxd, include_file_uses_normalized_filename_and_length_symbol) {
  TempDir tmp;
  tmp.write("hello-world.bin", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-i", L"hello-world.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "unsigned char hello_world_bin[] = {\n"
                 "  0x68, 0x65, 0x6c, 0x6c, 0x6f\n"
                 "};\n"
                 "unsigned int hello_world_bin_len = 5;\n");
}

TEST(xxd, include_explicit_name_normalizes_identifier) {
  TempDir tmp;
  tmp.write("input.bin", "AB");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"--include", L"--name", L"my-data.v1", L"input.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "unsigned char my_data_v1[] = {\n"
                 "  0x41, 0x42\n"
                 "};\n"
                 "unsigned int my_data_v1_len = 2;\n");
}

TEST(xxd, include_leading_digit_gets_reserved_prefix) {
  TempDir tmp;
  tmp.write("9patch.bin", "Z");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-i", L"9patch.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "unsigned char __9patch_bin[] = {\n"
                 "  0x5a\n"
                 "};\n"
                 "unsigned int __9patch_bin_len = 1;\n");
}

TEST(xxd, include_stdin_emits_values_without_implicit_identifier) {
  TempDir tmp;
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hi");
  p.add(L"xxd.exe", {L"-i"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "  0x68, 0x69\n");
}

TEST(xxd, include_preserves_uppercase_and_custom_columns) {
  TempDir tmp;
  tmp.write_bytes("input.bin", {'\xAB', '\xCD', '\xEF'});

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xxd.exe", {L"-i", L"-u", L"-c", L"2", L"input.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "unsigned char input_bin[] = {\n"
                 "  0xAB, 0xCD,\n"
                 "  0xEF\n"
                 "};\n"
                 "unsigned int input_bin_len = 3;\n");
}
