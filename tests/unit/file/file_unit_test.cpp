// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(file, file_basic) {
  TempDir tmp;
  tmp.write("test.txt", "Hello, World!");
  tmp.write("script.py", "print('hello')");

  TEST_LOG_FILE_CONTENT("test.txt", "Hello, World!");
  TEST_LOG_FILE_CONTENT("script.py", "print('hello')");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"test.txt", L"script.py"});

  TEST_LOG_CMD_LIST("file.exe", L"test.txt", L"script.py");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] Issue #1383: classification is by content only — a .py file
  // without a shebang is plain ASCII text, never "Python script".
  EXPECT_TRUE(r.stdout_text.find("ASCII text") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Python script") == std::string::npos);
}

TEST(file, file_brief) {
  TempDir tmp;
  tmp.write("document.pdf", "%PDF-1.4");

  TEST_LOG_FILE_CONTENT("document.pdf", "%PDF-1.4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"-b", L"document.pdf"});

  TEST_LOG_CMD_LIST("file.exe", L"-b", L"document.pdf");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe -b output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // In brief mode, should only show type without filename
  EXPECT_TRUE(r.stdout_text.find("PDF") != std::string::npos);
}

TEST(file, file_mime) {
  TempDir tmp;
  tmp.write("notes.txt", "Hello, World!");
  tmp.write("payload.bin", "%PDF-1.4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"--mime", L"notes.txt", L"payload.bin"});

  TEST_LOG_CMD_LIST("file.exe", L"--mime", L"notes.txt", L"payload.bin");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe --mime output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("notes.txt: text/plain; charset=us-ascii") !=
              std::string::npos);
  EXPECT_TRUE(
      r.stdout_text.find("payload.bin: application/pdf; charset=binary") !=
      std::string::npos);
}

TEST(file, file_directory) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "subdir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"subdir"});

  TEST_LOG_CMD_LIST("file.exe", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("directory") != std::string::npos);
}

TEST(file, file_nonexistent) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("file.exe", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("file.exe nonexistent output", r.stdout_text);

  // [GNU] Issue #1383: a missing operand is a stdout "cannot open" line
  // and the exit status stays 0.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("cannot open 'nonexistent.txt'") !=
              std::string::npos);
}

TEST(file, file_content_beats_extension) {
  TempDir tmp;
  tmp.write("photo.jpg", "just some plain text\n");
  tmp.write("notes.txt", "%PDF-1.4 fake\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"photo.jpg", L"notes.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("photo.jpg: ASCII text") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("notes.txt: PDF document") !=
              std::string::npos);
}

TEST(file, file_empty_and_shebang) {
  TempDir tmp;
  tmp.write("empty.bin", "");
  tmp.write("tool", "#!/usr/bin/env python3\nprint('hi')\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"empty.bin", L"tool"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("empty.bin: empty") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Python script") != std::string::npos);
}

TEST(file, file_no_operands_usage) {
  Pipeline p;
  p.add(L"file.exe", {});

  auto r = p.run();

  // [GNU] Issue #1383: no operands -> usage on stderr, exit 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("Usage: file") != std::string::npos);
}

// [refs #1127] --mime-encoding prints only the charset part.
TEST(file, mime_encoding_reports_charset) {
  TempDir tmp;
  tmp.write("ascii.txt", "plain ascii\n");
  tmp.write("utf8.txt", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"file.exe", {L"--mime-encoding", L"ascii.txt", L"utf8.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("ascii.txt: us-ascii") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("utf8.txt: utf-8") != std::string::npos);
}

// [refs #1127] --extension and --apple derive from the signature table.
TEST(file, extension_and_apple_from_signatures) {
  TempDir tmp;
  tmp.write("doc.pdf", "%PDF-1.7\n");
  tmp.write("pic.gif", "GIF89a");

  Pipeline ext;
  ext.set_cwd(tmp.wpath());
  ext.add(L"file.exe", {L"--extension", L"doc.pdf", L"pic.gif"});
  auto r_ext = ext.run();
  EXPECT_EQ(r_ext.exit_code, 0);
  EXPECT_TRUE(r_ext.stdout_text.find("doc.pdf: pdf") != std::string::npos);
  EXPECT_TRUE(r_ext.stdout_text.find("pic.gif: gif") != std::string::npos);

  Pipeline apple;
  apple.set_cwd(tmp.wpath());
  apple.add(L"file.exe", {L"--apple", L"pic.gif"});
  auto r_apple = apple.run();
  EXPECT_EQ(r_apple.exit_code, 0);
  EXPECT_TRUE(r_apple.stdout_text.find("pic.gif: 8BIMGIFf") !=
              std::string::npos);
}

// [refs #1127] -e soft removes the magic tests, so a PDF falls through
// to the text analysis; an unknown test errors for --exclude but is
// ignored by --exclude-quiet.
TEST(file, exclude_soft_and_unknown_test_handling) {
  TempDir tmp;
  tmp.write("doc.pdf", "%PDF-1.7 binary-ish\n");

  Pipeline excluded;
  excluded.set_cwd(tmp.wpath());
  excluded.add(L"file.exe", {L"--exclude", L"soft", L"doc.pdf"});
  auto r_ex = excluded.run();
  EXPECT_EQ(r_ex.exit_code, 0);
  EXPECT_TRUE(r_ex.stdout_text.find("PDF document") == std::string::npos);

  Pipeline bogus;
  bogus.set_cwd(tmp.wpath());
  bogus.add(L"file.exe", {L"--exclude", L"nosuchtest", L"doc.pdf"});
  auto r_bogus = bogus.run();
  EXPECT_EQ(r_bogus.exit_code, 1);
  EXPECT_TRUE(r_bogus.stderr_text.find("is not a valid test") !=
              std::string::npos);

  Pipeline quiet;
  quiet.set_cwd(tmp.wpath());
  quiet.add(L"file.exe", {L"--exclude-quiet", L"nosuchtest", L"doc.pdf"});
  auto r_quiet = quiet.run();
  EXPECT_EQ(r_quiet.exit_code, 0);
  EXPECT_TRUE(r_quiet.stdout_text.find("PDF document") != std::string::npos);
}

// [refs #1127] --list dumps the built-in table without operands, and
// --compile reports the platform difference.
TEST(file, list_and_compile_modes) {
  Pipeline list;
  list.add(L"file.exe", {L"--list"});
  auto r_list = list.run();
  EXPECT_EQ(r_list.exit_code, 0);
  EXPECT_TRUE(r_list.stdout_text.find("PDF document") != std::string::npos);

  Pipeline compile;
  compile.add(L"file.exe", {L"--compile"});
  auto r_compile = compile.run();
  EXPECT_EQ(r_compile.exit_code, 1);
  EXPECT_TRUE(r_compile.stderr_text.find("--compile is not available") !=
              std::string::npos);
}
