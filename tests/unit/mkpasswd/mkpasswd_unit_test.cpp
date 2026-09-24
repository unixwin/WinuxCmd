#include <algorithm>

#include "framework/winuxtest.h"

namespace {

std::string first_line(std::string text) {
  auto pos = text.find('\n');
  if (pos != std::string::npos) text.resize(pos);
  if (!text.empty() && text.back() == '\r') text.pop_back();
  return text;
}

size_t count_char(std::string_view text, char needle) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), needle));
}

}  // namespace

TEST(mkpasswd, current_outputs_passwd_record) {
  Pipeline p;
  p.add(L"mkpasswd.exe", {L"--current"});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  ASSERT_FALSE(r.stdout_text.empty());
  auto line = first_line(r.stdout_text);
  EXPECT_EQ(count_char(line, ':'), 6U);
  EXPECT_CONTAINS(line, ":*:");
  EXPECT_CONTAINS(line, ":Windows user:");
  EXPECT_CONTAINS(line, ":/home/");
  EXPECT_CONTAINS(line, ":/bin/sh");
}

TEST(mkpasswd, current_honors_home_prefix_and_shell) {
  Pipeline p;
  p.add(L"mkpasswd.exe",
        {L"--current", L"--path-prefix", L"/u", L"--shell", L"/bin/false"});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  auto line = first_line(r.stdout_text);
  EXPECT_EQ(count_char(line, ':'), 6U);
  EXPECT_CONTAINS(line, ":/u/");
  EXPECT_CONTAINS(line, ":/bin/false");
}

TEST(mkpasswd, rejects_unexpected_operand) {
  Pipeline p;
  p.add(L"mkpasswd.exe", {L"extra"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "unexpected operand");
}
