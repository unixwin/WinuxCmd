#include "framework/winuxtest.h"

TEST(base32, base32_encode_basic) {
  Pipeline p;
  p.set_stdin("hello world");
  p.add(L"base32.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "NBSWY3DPEB3W64TMMQ======\n");
}

TEST(base32, base32_decode_basic) {
  Pipeline p;
  p.set_stdin("NBSWY3DPEB3W64TMMQ======");
  p.add(L"base32.exe", {L"-d"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello world");
}

TEST(base32, base32_encode_file_and_stdin_dash) {
  TempDir tmp;
  tmp.write("plain.txt", "hello");

  Pipeline file_pipeline;
  file_pipeline.set_cwd(tmp.wpath());
  file_pipeline.add(L"base32.exe", {L"plain.txt"});

  auto file_result = file_pipeline.run();

  EXPECT_EQ(file_result.exit_code, 0);
  EXPECT_EQ_TEXT(file_result.stdout_text, "NBSWY3DP\n");

  Pipeline stdin_pipeline;
  stdin_pipeline.set_stdin("hello");
  stdin_pipeline.add(L"base32.exe", {L"-"});

  auto stdin_result = stdin_pipeline.run();

  EXPECT_EQ(stdin_result.exit_code, 0);
  EXPECT_EQ_TEXT(stdin_result.stdout_text, "NBSWY3DP\n");
}

TEST(base32, base32_wrap_width_and_wrap_zero) {
  Pipeline wrapped;
  wrapped.set_stdin("hello world");
  wrapped.add(L"base32.exe", {L"-w", L"8"});

  auto wrapped_result = wrapped.run();

  EXPECT_EQ(wrapped_result.exit_code, 0);
  EXPECT_EQ_TEXT(wrapped_result.stdout_text, "NBSWY3DP\nEB3W64TM\nMQ======\n");

  Pipeline unwrapped;
  unwrapped.set_stdin("hello world");
  unwrapped.add(L"base32.exe", {L"--wrap=0"});

  auto unwrapped_result = unwrapped.run();

  EXPECT_EQ(unwrapped_result.exit_code, 0);
  EXPECT_EQ_TEXT(unwrapped_result.stdout_text, "NBSWY3DPEB3W64TMMQ======\n");
}

TEST(base32, base32_empty_input_produces_no_output) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"base32.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(base32, base32_decode_reports_invalid_input_without_ignore_garbage) {
  Pipeline p;
  p.set_stdin("NBSW$Y3DP");
  p.add(L"base32.exe", {L"-d"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid input") != std::string::npos);
}

TEST(base32, base32_decode_ignore_garbage_recovers_payload) {
  Pipeline p;
  p.set_stdin("NBSW$Y3DP");
  p.add(L"base32.exe", {L"-d", L"--ignore-garbage"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello");
}

TEST(base32, base32_rejects_extra_file_operand) {
  TempDir tmp;
  tmp.write("one.txt", "one");
  tmp.write("two.txt", "two");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"base32.exe", {L"one.txt", L"two.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("extra operand") != std::string::npos);
}
