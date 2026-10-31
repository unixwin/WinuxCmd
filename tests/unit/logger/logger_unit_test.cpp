#include "framework/winuxtest.h"

TEST(logger, stderr_option_prints_tagged_message) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"hello", L"world"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "unit: hello world");
}

TEST(logger, accepts_priority_and_empty_stdin) {
  Pipeline p;
  p.add(L"logger.exe", {L"-p", L"user.warning"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

// [refs #1127] logger previously concatenated all of stdin into one
// message; GNU logs each input line as its own message (logger.c
// logger_stdin).
TEST(logger, stdin_lines_are_separate_messages) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit"});
  p.set_stdin("line one\nline two\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "line one");
  EXPECT_CONTAINS(r.stderr_text, "line two");
  EXPECT_TRUE(r.stderr_text.find("line oneline two") == std::string::npos);
}

// [refs #1127] -f logs the file's lines (previously declared but dead).
TEST(logger, file_option_logs_each_line) {
  TempDir tmp;
  tmp.write("messages.txt", "alpha line\nbeta line\n");

  Pipeline p;
  p.add(L"logger.exe",
        {L"-s", L"-t", L"unit", L"-f", tmp.wpath("messages.txt")});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "alpha line");
  EXPECT_CONTAINS(r.stderr_text, "beta line");
}

// [refs #1127] --prio-prefix strips a leading <NNN> from stdin lines.
TEST(logger, prio_prefix_is_stripped) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"--prio-prefix"});
  p.set_stdin("<13>prefixed hello\nplain line\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "prefixed hello");
  EXPECT_TRUE(r.stderr_text.find("<13>") == std::string::npos);
  EXPECT_CONTAINS(r.stderr_text, "plain line");
}

// [refs #1127] -S truncates a message to the given size.
TEST(logger, size_truncates_message) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"-S", L"5", L"abcdefghij"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "unit: abcde");
  EXPECT_TRUE(r.stderr_text.find("abcdef") == std::string::npos);
}

// [refs #1127] -i adds the [pid] bracket to the rendered message.
TEST(logger, id_adds_pid_bracket) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"-i", L"with", L"pid"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "unit[");
  EXPECT_CONTAINS(r.stderr_text, "]: with pid");
}

// [refs #1127] --no-act still prints with -s but performs no write.
TEST(logger, no_act_prints_without_writing) {
  Pipeline p;
  p.add(L"logger.exe", {L"-s", L"-t", L"unit", L"--no-act", L"dry", L"run"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "unit: dry run");
}

// [refs #1127] An invalid --socket-errors value warns and falls back
// to auto (logger.c:897).
TEST(logger, invalid_socket_errors_value_warns) {
  Pipeline p;
  p.add(L"logger.exe",
        {L"--socket-errors", L"bogus", L"-s", L"-t", L"unit", L"hi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "invalid argument");
}

// [refs #1127] --journald reads a FIELD=value entry and logs MESSAGE.
TEST(logger, journald_entry_message_is_logged) {
  Pipeline p;
  p.add(L"logger.exe", {L"--journald", L"-s", L"-t", L"unit"});
  p.set_stdin("MESSAGE=hello journal\nPRIORITY=5\nSYSLOG_IDENTIFIER=svc\n\n");

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "hello journal");
}

// [refs #1127] --msgid/--sd-id/--sd-param are accepted (they shape the
// RFC 5424 wire format for remote destinations).
TEST(logger, structured_data_options_accepted) {
  Pipeline p;
  p.add(L"logger.exe",
        {L"--msgid", L"ID47", L"--sd-id", L"exampleSDID@32473", L"--sd-param",
         L"iut=\"3\"", L"-s", L"-t", L"unit", L"sd", L"test"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_CONTAINS(r.stderr_text, "unit: sd test");
}
