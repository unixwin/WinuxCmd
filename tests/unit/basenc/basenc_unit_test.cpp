#include "framework/winuxtest.h"

TEST(basenc, basenc_requires_explicit_selector) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"basenc.exe", {});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("missing encoding type") != std::string::npos);
}

TEST(basenc, basenc_base64_selector_encode_decode_and_wrap) {
  Pipeline encoded;
  encoded.set_stdin("hello world");
  encoded.add(L"basenc.exe", {L"--base64", L"-w", L"4"});

  auto encoded_result = encoded.run();

  EXPECT_EQ(encoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(encoded_result.stdout_text, "aGVs\nbG8g\nd29y\nbGQ=\n");

  Pipeline decoded;
  decoded.set_stdin("aGVsbG8gd29ybGQ=");
  decoded.add(L"basenc.exe", {L"--base64", L"-d"});

  auto decoded_result = decoded.run();

  EXPECT_EQ(decoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(decoded_result.stdout_text, "hello world");
}

TEST(basenc, basenc_base64url_selector_uses_url_safe_alphabet) {
  Pipeline p;
  p.set_stdin(std::string("\xfb\xff", 2));
  p.add(L"basenc.exe", {L"--base64url", L"--wrap=0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-_8=");
}

TEST(basenc, basenc_base32_decode_is_implemented) {
  Pipeline p;
  p.set_stdin("NBSWY3DP");
  p.add(L"basenc.exe", {L"--base32", L"-d"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello");
}

TEST(basenc, basenc_base32hex_selector) {
  Pipeline encoded;
  encoded.set_stdin("hello");
  encoded.add(L"basenc.exe", {L"--base32hex"});

  auto encoded_result = encoded.run();

  EXPECT_EQ(encoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(encoded_result.stdout_text, "D1IMOR3F\n");

  Pipeline decoded;
  decoded.set_stdin("D1IMOR3F");
  decoded.add(L"basenc.exe", {L"--base32hex", L"-d"});

  auto decoded_result = decoded.run();

  EXPECT_EQ(decoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(decoded_result.stdout_text, "hello");
}

TEST(basenc, basenc_base16_selector_encode_decode) {
  Pipeline encoded;
  encoded.set_stdin("hello");
  encoded.add(L"basenc.exe", {L"--base16"});

  auto encoded_result = encoded.run();

  EXPECT_EQ(encoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(encoded_result.stdout_text, "68656C6C6F\n");

  Pipeline decoded;
  decoded.set_stdin("68656C6C6F");
  decoded.add(L"basenc.exe", {L"--base16", L"-d"});

  auto decoded_result = decoded.run();

  EXPECT_EQ(decoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(decoded_result.stdout_text, "hello");
}

TEST(basenc, basenc_base2_selectors) {
  Pipeline msbf;
  msbf.set_stdin("A");
  msbf.add(L"basenc.exe", {L"--base2msbf"});

  auto msbf_result = msbf.run();

  EXPECT_EQ(msbf_result.exit_code, 0);
  EXPECT_EQ_TEXT(msbf_result.stdout_text, "01000001\n");

  Pipeline lsbf;
  lsbf.set_stdin("A");
  lsbf.add(L"basenc.exe", {L"--base2lsbf"});

  auto lsbf_result = lsbf.run();

  EXPECT_EQ(lsbf_result.exit_code, 0);
  EXPECT_EQ_TEXT(lsbf_result.stdout_text, "10000010\n");
}

TEST(basenc, basenc_decode_reports_invalid_input_without_ignore_garbage) {
  Pipeline p;
  p.set_stdin("aGVs!bG8=");
  p.add(L"basenc.exe", {L"--base64", L"-d"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid input") != std::string::npos);
}

TEST(basenc, basenc_decode_ignore_garbage_recovers_payload) {
  Pipeline p;
  p.set_stdin("aGVs!bG8=");
  p.add(L"basenc.exe", {L"--base64", L"-d", L"--ignore-garbage"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello");
}

TEST(basenc, basenc_rejects_multiple_selectors) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"basenc.exe", {L"--base64", L"--base32"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("multiple encoding options") !=
              std::string::npos);
}

TEST(basenc, basenc_rejects_extra_file_operand_with_help_hint) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"basenc.exe", {L"--base64", L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "basenc: extra operand 'two.txt'\n"
                 "Try 'basenc --help' for more information.\n");
}

TEST(basenc, basenc_missing_input_reports_no_such_file) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"basenc.exe", {L"--base64", L"missing.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "basenc: cannot open 'missing.txt' for reading: No such "
                  "file or directory") != std::string::npos);
}

TEST(basenc, basenc_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"basenc.exe", {L"--base64", L"indir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find(
                  "basenc: cannot open 'indir' for reading: Is a directory") !=
              std::string::npos);
}

TEST(basenc, basenc_file_operand_glob_expands_single_match) {
  TempDir tmp;
  tmp.write("one.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"basenc.exe", {L"--base64", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "aGVsbG8=\n");
}

TEST(basenc, basenc_rejects_wildcard_that_expands_to_multiple_files) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"basenc.exe", {L"--base64", L"*.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "basenc: extra operand 'two.txt'\n"
                 "Try 'basenc --help' for more information.\n");
}

TEST(basenc, basenc_base58_selector_encode_decode) {
  Pipeline base58;
  base58.set_stdin("hello world");
  base58.add(L"basenc.exe", {L"--base58", L"--wrap=0"});

  auto base58_result = base58.run();

  EXPECT_EQ(base58_result.exit_code, 0);
  EXPECT_EQ_TEXT(base58_result.stdout_text, "StV1DL6CwTryKyV");

  Pipeline decoded;
  decoded.set_stdin("StV1DL6CwTryKyV");
  decoded.add(L"basenc.exe", {L"--base58", L"-d"});

  auto decoded_result = decoded.run();

  EXPECT_EQ(decoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(decoded_result.stdout_text, "hello world");
}

TEST(basenc, basenc_z85_selector_encode_decode) {
  Pipeline z85;
  z85.set_stdin(std::string("\x86\x4f\xd2\x6f", 4));
  z85.add(L"basenc.exe", {L"--z85", L"--wrap=0"});

  auto z85_result = z85.run();

  EXPECT_EQ(z85_result.exit_code, 0);
  EXPECT_EQ_TEXT(z85_result.stdout_text, "Hello");

  Pipeline decoded;
  decoded.set_stdin("Hello");
  decoded.add(L"basenc.exe", {L"--z85", L"-d"});

  auto decoded_result = decoded.run();

  EXPECT_EQ(decoded_result.exit_code, 0);
  EXPECT_EQ_TEXT(decoded_result.stdout_text,
                 std::string("\x86\x4f\xd2\x6f", 4));
}

TEST(basenc, basenc_z85_rejects_non_block_sized_input) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"basenc.exe", {L"--z85"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "invalid input length for z85 encoding");
}

TEST(basenc, basenc_z85_decode_rejects_values_outside_32_bit_range) {
  Pipeline p;
  p.set_stdin("#####");
  p.add(L"basenc.exe", {L"--z85", L"-d"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "invalid input");
}
