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

TEST(mkgroup, current_outputs_group_records) {
  Pipeline p;
  p.add(L"mkgroup.exe", {L"--current"});

  auto r = p.run();

  ASSERT_EQ(r.exit_code, 0);
  ASSERT_FALSE(r.stdout_text.empty());
  auto line = first_line(r.stdout_text);
  EXPECT_EQ(count_char(line, ':'), 3U);
  EXPECT_CONTAINS(line, ":*:");
}

TEST(mkgroup, local_option_is_accepted) {
  Pipeline p;
  p.add(L"mkgroup.exe", {L"--local"});

  auto r = p.run();

  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
  if (r.exit_code == 0) {
    auto line = first_line(r.stdout_text);
    EXPECT_EQ(count_char(line, ':'), 3U);
    EXPECT_CONTAINS(line, ":*:");
  }
}

TEST(mkgroup, rejects_unexpected_operand) {
  Pipeline p;
  p.add(L"mkgroup.exe", {L"extra"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "unexpected operand");
}
