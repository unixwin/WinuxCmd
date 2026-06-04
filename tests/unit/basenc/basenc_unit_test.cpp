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
  EXPECT_EQ_TEXT(r.stdout_text, "-_8=\n");
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

TEST(basenc, basenc_recognizes_unimplemented_gnu_selectors) {
  Pipeline base58;
  base58.set_stdin("hello");
  base58.add(L"basenc.exe", {L"--base58"});

  auto base58_result = base58.run();

  EXPECT_NE(base58_result.exit_code, 0);
  EXPECT_TRUE(base58_result.stderr_text.find("base58 encoding is not "
                                             "implemented") !=
              std::string::npos);

  Pipeline z85;
  z85.set_stdin("hello");
  z85.add(L"basenc.exe", {L"--z85"});

  auto z85_result = z85.run();

  EXPECT_NE(z85_result.exit_code, 0);
  EXPECT_TRUE(z85_result.stderr_text.find("z85 encoding is not implemented") !=
              std::string::npos);
}
