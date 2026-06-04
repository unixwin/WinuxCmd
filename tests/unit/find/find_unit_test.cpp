/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: find_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

namespace {

bool create_directory_symlink_or_skip(const std::filesystem::path& link,
                                      const std::filesystem::path& target) {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif

  if (CreateSymbolicLinkW(link.wstring().c_str(), target.wstring().c_str(),
                          flags)) {
    return true;
  }

  std::cout << "  SKIPPED (CreateSymbolicLinkW failed with error "
            << GetLastError() << ")\n";
  return false;
}

}  // namespace

bool is_octal_mode(std::string_view text) {
  if (text.size() != 3 && text.size() != 4) return false;
  for (char ch : text) {
    if (ch < '0' || ch > '7') return false;
  }
  return true;
}

bool is_seconds_with_fraction(std::string_view text) {
  auto dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == text.size()) {
    return false;
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == dot) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  return true;
}

TEST(find, find_name_pattern) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  tmp.write("src/a.cpp", "");
  tmp.write("src/b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"src", L"-name", L"*.cpp"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/a.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/b.txt") == std::string::npos);
}

TEST(find, find_iname_and_type) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "Dir");
  tmp.write("Dir/ReadMe.MD", "");
  tmp.write("Dir/file.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"Dir", L"-type", L"f", L"-iname", L"readme.*"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Dir/ReadMe.MD") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file.txt") == std::string::npos);
}

TEST(find, find_path_pattern_matches_full_path) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src" / "nested");
  tmp.write("src/nested/match.txt", "");
  tmp.write("src/nested/skip.log", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"src", L"-path", L"src/*/*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/nested/match.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/nested/skip.log") == std::string::npos);
}

TEST(find, find_ipath_pattern_is_case_insensitive) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "Case" / "Inner");
  tmp.write("Case/Inner/ReadMe.TXT", "");
  tmp.write("Case/Inner/Other.log", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"Case", L"-ipath", L"case/*/*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Case/Inner/ReadMe.TXT") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Case/Inner/Other.log") == std::string::npos);
}

TEST(find, find_maxdepth) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a" / "b");
  tmp.write("a/top.txt", "");
  tmp.write("a/b/deep.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a", L"-maxdepth", L"1", L"-name", L"*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a/top.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a/b/deep.txt") == std::string::npos);
}

TEST(find, find_quit_stops_after_first_match) {
  TempDir tmp;
  tmp.write("first.txt", "");
  tmp.write("second.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"*.txt", L"-quit"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  size_t newline_count = 0;
  for (char ch : r.stdout_text) {
    if (ch == '\n') ++newline_count;
  }
  EXPECT_EQ(newline_count, 1u);
}

TEST(find, find_empty_matches_empty_files_and_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "emptydir");
  std::filesystem::create_directories(tmp.path / "nonemptydir");
  tmp.write("empty.txt", "");
  tmp.write("nonempty.txt", "content");
  tmp.write("nonemptydir/child.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-empty"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("empty.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("emptydir") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nonempty.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nonemptydir\n") == std::string::npos);
}

TEST(find, find_size_supports_byte_and_kib_units) {
  TempDir tmp;
  tmp.write("small.bin", "12345");
  tmp.write("large.bin", std::string(2000, 'x'));

  Pipeline exact_bytes;
  exact_bytes.set_cwd(tmp.wpath());
  exact_bytes.add(L"find.exe", {L".", L"-type", L"f", L"-size", L"5c"});
  auto exact_result = exact_bytes.run();

  EXPECT_EQ(exact_result.exit_code, 0);
  EXPECT_TRUE(exact_result.stdout_text.find("small.bin") != std::string::npos);
  EXPECT_TRUE(exact_result.stdout_text.find("large.bin") == std::string::npos);

  Pipeline kib_rounded;
  kib_rounded.set_cwd(tmp.wpath());
  kib_rounded.add(L"find.exe", {L".", L"-type", L"f", L"-size", L"+1k"});
  auto kib_result = kib_rounded.run();

  EXPECT_EQ(kib_result.exit_code, 0);
  EXPECT_TRUE(kib_result.stdout_text.find("large.bin") != std::string::npos);
  EXPECT_TRUE(kib_result.stdout_text.find("small.bin") == std::string::npos);
}

TEST(find, find_mmin_and_mtime_support_signed_ranges) {
  TempDir tmp;
  tmp.write("recent.txt", "now");
  tmp.write("older.txt", "old");

  auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "older.txt",
                                   now - std::chrono::hours(49));

  Pipeline older_minutes;
  older_minutes.set_cwd(tmp.wpath());
  older_minutes.add(L"find.exe", {L".", L"-type", L"f", L"-mmin", L"+60"});
  auto minute_result = older_minutes.run();

  EXPECT_EQ(minute_result.exit_code, 0);
  EXPECT_TRUE(minute_result.stdout_text.find("older.txt") != std::string::npos);
  EXPECT_TRUE(minute_result.stdout_text.find("recent.txt") ==
              std::string::npos);

  Pipeline older_days;
  older_days.set_cwd(tmp.wpath());
  older_days.add(L"find.exe", {L".", L"-type", L"f", L"-mtime", L"+1"});
  auto day_result = older_days.run();

  EXPECT_EQ(day_result.exit_code, 0);
  EXPECT_TRUE(day_result.stdout_text.find("older.txt") != std::string::npos);
  EXPECT_TRUE(day_result.stdout_text.find("recent.txt") == std::string::npos);
}

TEST(find, find_true_and_false_are_expression_primaries) {
  TempDir tmp;
  tmp.write("keep.txt", "");
  tmp.write("skip.log", "");

  Pipeline true_pipeline;
  true_pipeline.set_cwd(tmp.wpath());
  true_pipeline.add(L"find.exe", {L".", L"-true", L"-name", L"*.txt"});
  auto true_result = true_pipeline.run();

  EXPECT_EQ(true_result.exit_code, 0);
  EXPECT_TRUE(true_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(true_result.stdout_text.find("skip.log") == std::string::npos);

  Pipeline false_pipeline;
  false_pipeline.set_cwd(tmp.wpath());
  false_pipeline.add(L"find.exe", {L".", L"-false"});
  auto false_result = false_pipeline.run();

  EXPECT_EQ(false_result.exit_code, 0);
  EXPECT_TRUE(false_result.stdout_text.empty());
}

TEST(find, find_regex_and_iregex_match_whole_path) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("docs/ReadMe.MD", "");
  tmp.write("docs/notes.txt", "");

  Pipeline regex_pipeline;
  regex_pipeline.set_cwd(tmp.wpath());
  regex_pipeline.add(L"find.exe", {L".", L"-regex", L"./src/.*\\.cpp"});
  auto regex_result = regex_pipeline.run();

  EXPECT_EQ(regex_result.exit_code, 0);
  EXPECT_TRUE(regex_result.stdout_text.find("src/main.cpp") !=
              std::string::npos);
  EXPECT_TRUE(regex_result.stdout_text.find("docs/ReadMe.MD") ==
              std::string::npos);

  Pipeline iregex_pipeline;
  iregex_pipeline.set_cwd(tmp.wpath());
  iregex_pipeline.add(L"find.exe", {L".", L"-iregex", L"./docs/readme\\.md"});
  auto iregex_result = iregex_pipeline.run();

  EXPECT_EQ(iregex_result.exit_code, 0);
  EXPECT_TRUE(iregex_result.stdout_text.find("docs/ReadMe.MD") !=
              std::string::npos);
  EXPECT_TRUE(iregex_result.stdout_text.find("docs/notes.txt") ==
              std::string::npos);
}

TEST(find, find_newer_matches_files_modified_after_reference) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("reference.txt", "ref");
  tmp.write("new.txt", "new");

  auto now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(tmp.path / "old.txt",
                                   now - std::chrono::hours(2));
  std::filesystem::last_write_time(tmp.path / "reference.txt",
                                   now - std::chrono::hours(1));
  std::filesystem::last_write_time(tmp.path / "new.txt", now);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-type", L"f", L"-newer", L"reference.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("new.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("old.txt") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("reference.txt") == std::string::npos);
}

TEST(find, find_depth_processes_directory_contents_first) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "dir");
  tmp.write("tree/dir/file.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-depth"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  auto file_pos = r.stdout_text.find("tree/dir/file.txt\n");
  auto dir_pos = r.stdout_text.find("tree/dir\n");
  auto root_pos = r.stdout_text.find("tree\n");

  EXPECT_TRUE(file_pos != std::string::npos);
  EXPECT_TRUE(dir_pos != std::string::npos);
  EXPECT_TRUE(root_pos != std::string::npos);
  if (file_pos == std::string::npos || dir_pos == std::string::npos ||
      root_pos == std::string::npos) {
    return;
  }
  EXPECT_LT(file_pos, dir_pos);
  EXPECT_LT(dir_pos, root_pos);
}

TEST(find, find_expression_not_negates_next_primary) {
  TempDir tmp;
  tmp.write("keep.txt", "");
  tmp.write("skip.log", "");

  Pipeline bang;
  bang.set_cwd(tmp.wpath());
  bang.add(L"find.exe", {L".", L"-type", L"f", L"!", L"-name", L"*.log"});

  auto bang_result = bang.run();
  EXPECT_EQ(bang_result.exit_code, 0);
  EXPECT_TRUE(bang_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(bang_result.stdout_text.find("skip.log") == std::string::npos);

  Pipeline word;
  word.set_cwd(tmp.wpath());
  word.add(L"find.exe", {L".", L"-type", L"f", L"-not", L"-name", L"*.log"});

  auto word_result = word.run();
  EXPECT_EQ(word_result.exit_code, 0);
  EXPECT_TRUE(word_result.stdout_text.find("keep.txt") != std::string::npos);
  EXPECT_TRUE(word_result.stdout_text.find("skip.log") == std::string::npos);
}

TEST(find, find_expression_implicit_and_has_higher_precedence_than_or) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("docs/readme.md", "");
  tmp.write("docs/notes.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-path", L"./src/*", L"-name", L"*.cpp", L"-o",
                      L"-name", L"*.md"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/main.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/readme.md") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/notes.txt") == std::string::npos);
}

TEST(find, find_expression_explicit_and_aliases_match_implicit_and) {
  TempDir tmp;
  tmp.write("match.cpp", "");
  tmp.write("skip.cpp", "content");
  tmp.write("empty.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"-type", L"f", L"-a", L"-name", L"*.cpp", L"-a", L"-empty"},
      {L".", L"-type", L"f", L"-and", L"-name", L"*.cpp", L"-and", L"-empty"},
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-empty"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("match.cpp") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("skip.cpp") == std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("empty.txt") == std::string::npos);
  }
}

TEST(find, find_expression_or_aliases_match_either_side) {
  TempDir tmp;
  tmp.write("a.cpp", "");
  tmp.write("b.md", "");
  tmp.write("c.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-o", L"-name", L"*.md"},
      {L".", L"-type", L"f", L"-name", L"*.cpp", L"-or", L"-name", L"*.md"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.find("a.cpp") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("b.md") != std::string::npos);
    EXPECT_TRUE(r.stdout_text.find("c.txt") == std::string::npos);
  }
}

TEST(find, find_expression_parentheses_override_precedence) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "src");
  std::filesystem::create_directories(tmp.path / "docs");
  tmp.write("src/main.cpp", "");
  tmp.write("src/readme.md", "");
  tmp.write("docs/readme.md", "");
  tmp.write("src/notes.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-path", L"./src/*", L"(", L"-name", L"*.cpp",
                      L"-o", L"-name", L"*.md", L")"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("src/main.cpp") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/readme.md") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("docs/readme.md") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("src/notes.txt") == std::string::npos);
}

TEST(find, find_expression_syntax_errors_return_error) {
  TempDir tmp;
  tmp.write("a.txt", "");

  const std::vector<std::vector<std::wstring>> cases = {
      {L".", L"(", L"-name", L"*.txt"},
      {L".", L"-name", L"*.txt", L")"},
      {L".", L"!", L"-o", L"-name", L"*.txt"},
      {L".", L"-name", L"*.txt", L"-o"},
      {L".", L"-type", L"f", L"-a"},
  };

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
  }
}

TEST(find, find_print0_uses_nul_separator) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "a");
  tmp.write("a/with space.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a", L"-name", L"*.txt", L"-print0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a/with space.txt\0", 17));
}

TEST(find, find_printf_formats_common_file_fields) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "root");
  tmp.write("root/file.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"root", L"-type", L"f", L"-printf", L"%p|%f|%h|%y|%s|%m|%T@|%%\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);

  std::string line = r.stdout_text;
  if (!line.empty() && line.back() == '\n') line.pop_back();

  std::vector<std::string> fields;
  size_t start = 0;
  while (start <= line.size()) {
    auto sep = line.find('|', start);
    if (sep == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, sep - start));
    start = sep + 1;
  }

  EXPECT_EQ(fields.size(), 8u);
  if (fields.size() != 8u) return;
  EXPECT_EQ(fields[0], "root/file.txt");
  EXPECT_EQ(fields[1], "file.txt");
  EXPECT_EQ(fields[2], "root");
  EXPECT_EQ(fields[3], "f");
  EXPECT_EQ(fields[4], "5");
  EXPECT_TRUE(is_octal_mode(fields[5]));
  EXPECT_TRUE(is_seconds_with_fraction(fields[6]));
  EXPECT_EQ(fields[7], "%");
}

TEST(find, find_printf_suppresses_default_print_and_handles_escapes) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f\\t%y\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\tf\n");
}

TEST(find, find_printf_supports_nul_escape) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-printf", L"%f\\0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0", 6));
}

TEST(find, find_expression_scopes_print_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-print", L"-o",
                      L"-name", L"b.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\n");
}

TEST(find, find_expression_scopes_print0_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-print0", L"-o",
                      L"-name", L"b.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a.txt\0", 6));
}

TEST(find, find_expression_scopes_printf_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-printf", L"A:%f|%y|%s\\n",
         L"-o", L"-name", L"b.txt", L"-printf", L"B:%f|%y|%s\\n"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "A:a.txt|f|1\nB:b.txt|f|1\n");
}

TEST(find, find_expression_scopes_exec_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-exec", L"cmd.exe", L"/C",
         L"echo", L"EXEC:{}", L";", L"-o", L"-name", L"b.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("EXEC:a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("EXEC:b.txt") == std::string::npos);
}

TEST(find, find_expression_ok_decline_returns_false_for_or_fallback) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"find.exe", {L"a.txt", L"-ok", L"cmd.exe", L"/C", L"echo", L"OK:{}",
                      L";", L"-o", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\n");
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OK:a.txt") == std::string::npos);
}

TEST(find, find_expression_scopes_quit_to_reached_branch) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");
  tmp.write("c.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"c.txt", L"b.txt", L"-name", L"a.txt", L"-print", L"-o",
         L"-name", L"b.txt", L"-quit", L"-o", L"-name", L"c.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a.txt\nc.txt\n");
}

TEST(find, find_missing_path_returns_error) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"not_exists", L"-name", L"*.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(find, find_delete_removes_matching_file_without_default_print) {
  TempDir tmp;
  tmp.write("delete-me.txt", "x");
  tmp.write("keep.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"delete-me.txt", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "delete-me.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "keep.txt"));
}

TEST(find, find_expression_scopes_delete_to_branch) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "y");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"b.txt", L"-name", L"a.txt", L"-o", L"-name",
                      L"b.txt", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "a.txt"));
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "b.txt"));
}

TEST(find, find_delete_removes_nested_directories_depth_first) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree" / "a" / "b");
  tmp.write("tree/root.txt", "root");
  tmp.write("tree/a/mid.txt", "mid");
  tmp.write("tree/a/b/leaf.txt", "leaf");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(std::filesystem::exists(tmp.path / "tree"));
}

TEST(find, find_delete_returns_error_when_matched_directory_is_not_empty) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "tree");
  tmp.write("tree/unmatched.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"tree", L"-type", L"d", L"-delete"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "tree" / "unmatched.txt"));
}

TEST(find, find_exec_semicolon_runs_once_per_match) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.log", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"*.txt", L"-exec", L"cmd.exe", L"/C",
                      L"echo", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.log") == std::string::npos);
}

TEST(find, find_exec_replaces_placeholder_inside_argument) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe",
        {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo", L"file={}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file=a.txt") != std::string::npos);
}

TEST(find, find_exec_allows_child_arguments_that_look_like_options) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo",
                      L"-child-flag", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("-child-flag a.txt") != std::string::npos);
}

TEST(find, find_exec_plus_batches_paths) {
  TempDir tmp;
  tmp.write("a.txt", "x");
  tmp.write("b.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-type", L"f", L"-exec", L"cmd.exe", L"/C",
                      L"echo", L"files", L"{}", L"+"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("files") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(find, find_ok_prompts_and_respects_answer) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"find.exe",
        {L"a.txt", L"-ok", L"cmd.exe", L"/C", L"echo", L"{}", L";"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("cmd.exe") != std::string::npos);
}

TEST(find, find_exec_missing_terminator_returns_error) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"a.txt", L"-exec", L"cmd.exe", L"/C", L"echo", L"{}"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(find, find_prune_skips_descending_into_directories) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "keep" / "nested");
  tmp.write("keep/nested/match.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L"keep", L"-prune"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("keep/nested") == std::string::npos);
}

TEST(find, find_expression_scopes_prune_to_matching_branch) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "skip" / "nested");
  std::filesystem::create_directories(tmp.path / "keep");
  tmp.write("skip/nested/hidden.txt", "");
  tmp.write("keep/match.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-name", L"skip", L"-prune", L"-o", L"-type", L"f",
                      L"-name", L"*.txt", L"-print"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("keep/match.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("skip/nested/hidden.txt") ==
              std::string::npos);
}

TEST(find, find_L_follows_directory_symlink_if_available) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "work");
  std::filesystem::create_directories(tmp.path / "target" / "nested");
  tmp.write("target/nested/through-link.txt", "");

  if (!create_directory_symlink_or_skip(tmp.path / "work" / "linked",
                                        tmp.path / "target")) {
    return;
  }

  Pipeline p;
  p.set_cwd((tmp.path / "work").wstring());
  p.add(L"find.exe", {L"-L", L".", L"-name", L"through-link.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("linked/nested/through-link.txt") !=
              std::string::npos);
}

TEST(find, find_unsupported_flags_return_error) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  const std::vector<std::vector<std::wstring>> cases = {{L".", L"-H"}};

  for (const auto& args : cases) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"find.exe", args);

    auto r = p.run();
    EXPECT_EQ(r.exit_code, 1);
  }
}

TEST(find, find_invalid_size_returns_error) {
  TempDir tmp;
  tmp.write("a.txt", "x");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"find.exe", {L".", L"-size", L"bad"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}
