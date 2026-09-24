/*
 *  Copyright (c) 2026 WinuxCmd
 */
#include "framework/winuxtest.h"

static void write_binary_file(const std::filesystem::path& path,
                              std::string_view data) {
  std::ofstream out(path, std::ios::binary);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

TEST(hexdump, hexdump_canonical_matches_util_linux_shape) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdefABCDEFGHIJKLMNOP");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"-n", L"32", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000000  30 31 32 33 34 35 36 37  38 39 61 62 63 64 65 66  "
      "|0123456789abcdef|\n"
      "00000010  41 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  "
      "|ABCDEFGHIJKLMNOP|\n"
      "00000020\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_canonical_skip_and_length_preserve_offsets) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdefABCDEFGHIJKLMNOP");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"-s", L"16", L"-n", L"8", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000010  41 42 43 44 45 46 47 48                           |ABCDEFGH|\n"
      "00000018\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_default_is_two_byte_hex) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000 3130 3332 3534 3736 3938 6261 6463 6665\n"
      "0000010\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_hex2_explicit_uses_padded_words) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "0123456789abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-x", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000    3130    3332    3534    3736    3938    6261    6463    6665 "
      "\n"
      "0000010\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_octal1_prints_final_offset) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "ABC");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-b", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000 101 102 103                                                     "
      "\n"
      "0000003\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_char_escapes_control_chars) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "A\n\t");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-c", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "0000000   A  \\n  \\t                                                   "
      "  \n"
      "0000003\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_empty_canonical_is_silent) {
  TempDir tmp;
  write_binary_file(tmp.path / "empty.bin", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-C", L"empty.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(hexdump, hexdump_length) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "Hello World");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-n", L"5", L"-C", L"test.bin"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "00000000  48 65 6c 6c 6f                                    |Hello|\n"
      "00000005\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(hexdump, hexdump_stdin) {
  Pipeline p;
  p.set_stdin("Hello World");
  p.add(L"hexdump.exe", {L"-C"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("0000000b") != std::string::npos);
}

TEST(hexdump, hexdump_invalid_format_reports_error) {
  TempDir tmp;
  write_binary_file(tmp.path / "test.bin", "abc");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-e", L"%", L"test.bin"});
  auto r = p.run();
  EXPECT_NE(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(hexdump, hexdump_nonexistent_file) {
  Pipeline p;
  p.add(L"hexdump.exe", {L"nonexistent_file_xyz.bin"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}
