// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
module;

#ifdef WINUXCMD_ENABLE_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#ifndef PCRE2_STATIC
#define PCRE2_STATIC
#endif
#include <pcre2.h>
#endif

export module utils:regex;

import std;

export namespace portable_regex {

enum class Syntax { Basic, Extended, Perl };

struct Submatch {
  size_t begin = 0;
  size_t end = 0;
  bool matched = false;
};

struct Match {
  size_t begin = 0;
  size_t end = 0;
  std::vector<Submatch> captures;

  [[nodiscard]] auto group(size_t index) const -> std::optional<Submatch> {
    if (index >= captures.size() || !captures[index].matched) {
      return std::nullopt;
    }
    return captures[index];
  }
};

struct CompileResult;

namespace detail {

constexpr int kUnlimited = -1;
constexpr size_t kInvalidNode = static_cast<size_t>(-1);

auto fold_ascii(char ch) -> char {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

auto is_word_char(unsigned char c) -> bool {
  return std::isalnum(c) || c == '_';
}

enum class ClassAtomKind { Literal, Range, Posix };

struct ClassAtom {
  ClassAtomKind kind = ClassAtomKind::Literal;
  char first = '\0';
  char last = '\0';
  std::string name;
};

enum class NodeKind {
  Empty,
  Literal,
  Dot,
  AnchorStart,
  AnchorEnd,
  WordStart,
  WordEnd,
  CharClass,
  Sequence,
  Alternation,
  Capture,
  BackReference,
  Repeat
};

struct Node {
  NodeKind kind = NodeKind::Empty;
  char literal = '\0';
  bool negated = false;
  std::vector<ClassAtom> class_atoms;
  std::vector<size_t> children;
  size_t child = kInvalidNode;
  size_t capture_index = 0;
  int min_repeat = 0;
  int max_repeat = 0;
};

auto class_matches_posix(std::string_view name, unsigned char c) -> bool {
  if (name == "alnum") return std::isalnum(c) != 0;
  if (name == "alpha") return std::isalpha(c) != 0;
  if (name == "blank") return c == ' ' || c == '\t';
  if (name == "cntrl") return std::iscntrl(c) != 0;
  if (name == "digit") return std::isdigit(c) != 0;
  if (name == "graph") return std::isgraph(c) != 0;
  if (name == "lower") return std::islower(c) != 0;
  if (name == "print") return std::isprint(c) != 0;
  if (name == "punct") return std::ispunct(c) != 0;
  if (name == "space") return std::isspace(c) != 0;
  if (name == "upper") return std::isupper(c) != 0;
  if (name == "xdigit") return std::isxdigit(c) != 0;
  if (name == "word") return is_word_char(c);
  return false;
}

auto sorted_unique(std::vector<size_t> values) -> std::vector<size_t> {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

auto valid_range_bounds(int min_repeat, int max_repeat) -> bool {
  return min_repeat >= 0 &&
         (max_repeat == kUnlimited || max_repeat >= min_repeat);
}

class Parser {
 public:
  Parser(Syntax syntax, std::string_view pattern, bool ignore_case)
      : syntax_(syntax), pattern_(pattern), ignore_case_(ignore_case) {}

  auto parse() -> size_t {
    size_t root = parse_expression();
    if (failed()) return kInvalidNode;
    if (pos_ != pattern_.size()) {
      set_error("unexpected token in regular expression");
      return kInvalidNode;
    }
    return root;
  }

  auto nodes() && -> std::vector<Node> { return std::move(nodes_); }
  auto error() const -> const std::string& { return error_; }
  auto failed() const -> bool { return !error_.empty(); }
  auto ignore_case() const -> bool { return ignore_case_; }
  auto capture_count() const -> size_t { return capture_count_; }

 private:
  Syntax syntax_ = Syntax::Basic;
  std::string_view pattern_;
  bool ignore_case_ = false;
  size_t pos_ = 0;
  size_t capture_count_ = 0;
  std::string error_;
  std::vector<Node> nodes_;

  auto add_node(Node node) -> size_t {
    nodes_.push_back(std::move(node));
    return nodes_.size() - 1;
  }

  auto make_empty() -> size_t { return add_node(Node{}); }

  auto make_literal(char ch) -> size_t {
    Node n;
    n.kind = NodeKind::Literal;
    n.literal = ch;
    return add_node(std::move(n));
  }

  auto make_sequence(std::vector<size_t> children) -> size_t {
    if (children.empty()) return make_empty();
    if (children.size() == 1) return children.front();
    Node n;
    n.kind = NodeKind::Sequence;
    n.children = std::move(children);
    return add_node(std::move(n));
  }

  auto make_alternation(std::vector<size_t> children) -> size_t {
    if (children.empty()) return make_empty();
    if (children.size() == 1) return children.front();
    Node n;
    n.kind = NodeKind::Alternation;
    n.children = std::move(children);
    return add_node(std::move(n));
  }

  auto make_repeat(size_t child, int min_repeat, int max_repeat) -> size_t {
    Node n;
    n.kind = NodeKind::Repeat;
    n.child = child;
    n.min_repeat = min_repeat;
    n.max_repeat = max_repeat;
    return add_node(std::move(n));
  }

  auto make_capture(size_t child) -> size_t {
    Node n;
    n.kind = NodeKind::Capture;
    n.child = child;
    n.capture_index = ++capture_count_;
    return add_node(std::move(n));
  }

  auto make_back_reference(size_t capture_index) -> size_t {
    Node n;
    n.kind = NodeKind::BackReference;
    n.capture_index = capture_index;
    return add_node(std::move(n));
  }

  auto make_posix_class(std::string name, bool negated = false) -> size_t {
    Node n;
    n.kind = NodeKind::CharClass;
    n.negated = negated;
    n.class_atoms.push_back(
        ClassAtom{ClassAtomKind::Posix, '\0', '\0', std::move(name)});
    return add_node(std::move(n));
  }

  auto make_escape_class(char escaped) -> std::optional<size_t> {
    switch (escaped) {
      case 's':
        return make_posix_class("space");
      case 'S':
        return make_posix_class("space", true);
      case 'w':
        return make_posix_class("word");
      case 'W':
        return make_posix_class("word", true);
      default:
        return std::nullopt;
    }
  }

  auto peek(size_t offset = 0) const -> char {
    if (pos_ + offset >= pattern_.size()) return '\0';
    return pattern_[pos_ + offset];
  }

  auto consume() -> char {
    if (pos_ >= pattern_.size()) return '\0';
    return pattern_[pos_++];
  }

  auto consume_if(char ch) -> bool {
    if (peek() != ch) return false;
    ++pos_;
    return true;
  }

  auto set_error(std::string message) -> void {
    if (error_.empty()) error_ = std::move(message);
  }

  auto is_alt_at_current() const -> bool {
    if (syntax_ == Syntax::Extended) return peek() == '|';
    return peek() == '\\' && peek(1) == '|';
  }

  auto is_group_end_at_current() const -> bool {
    if (syntax_ == Syntax::Extended) return peek() == ')';
    return peek() == '\\' && peek(1) == ')';
  }

  auto consume_alt() -> void {
    if (syntax_ == Syntax::Extended) {
      ++pos_;
    } else {
      pos_ += 2;
    }
  }

  auto consume_group_end() -> void {
    if (syntax_ == Syntax::Extended) {
      ++pos_;
    } else {
      pos_ += 2;
    }
  }

  auto parse_expression() -> size_t {
    std::vector<size_t> branches;
    branches.push_back(parse_sequence());
    while (!failed() && is_alt_at_current()) {
      consume_alt();
      branches.push_back(parse_sequence());
    }
    return make_alternation(std::move(branches));
  }

  auto parse_sequence() -> size_t {
    std::vector<size_t> pieces;
    while (!failed() && pos_ < pattern_.size()) {
      if (is_alt_at_current() || is_group_end_at_current()) break;
      pieces.push_back(parse_piece());
    }
    return make_sequence(std::move(pieces));
  }

  auto parse_piece() -> size_t {
    size_t atom = parse_atom();
    while (!failed()) {
      if (consume_if('*')) {
        atom = make_repeat(atom, 0, kUnlimited);
        continue;
      }
      if (syntax_ == Syntax::Extended) {
        if (consume_if('+')) {
          atom = make_repeat(atom, 1, kUnlimited);
          continue;
        }
        if (consume_if('?')) {
          atom = make_repeat(atom, 0, 1);
          continue;
        }
        if (peek() == '{') {
          auto range = parse_interval(false);
          if (!range) return atom;
          atom = make_repeat(atom, range->first, range->second);
          continue;
        }
      } else if (peek() == '\\') {
        char next = peek(1);
        if (next == '+') {
          pos_ += 2;
          atom = make_repeat(atom, 1, kUnlimited);
          continue;
        }
        if (next == '?') {
          pos_ += 2;
          atom = make_repeat(atom, 0, 1);
          continue;
        }
        if (next == '{') {
          auto range = parse_interval(true);
          if (!range) return atom;
          atom = make_repeat(atom, range->first, range->second);
          continue;
        }
      }
      break;
    }
    return atom;
  }

  auto parse_atom() -> size_t {
    if (pos_ >= pattern_.size()) return make_empty();

    char ch = consume();
    if (ch == '^') {
      Node n;
      n.kind = NodeKind::AnchorStart;
      return add_node(std::move(n));
    }
    if (ch == '$') {
      Node n;
      n.kind = NodeKind::AnchorEnd;
      return add_node(std::move(n));
    }
    if (ch == '.') {
      Node n;
      n.kind = NodeKind::Dot;
      return add_node(std::move(n));
    }
    if (ch == '[') {
      return parse_char_class();
    }
    if (syntax_ == Syntax::Extended && ch == '(') {
      size_t expr = parse_expression();
      if (failed()) return expr;
      if (!consume_if(')')) {
        set_error("unmatched '(' in regular expression");
      }
      return make_capture(expr);
    }
    if (ch == '\\') {
      return parse_escape_atom();
    }
    if (syntax_ == Syntax::Extended && ch == ')') {
      set_error("unmatched ')' in regular expression");
      return make_empty();
    }
    return make_literal(ch);
  }

  auto parse_escape_atom() -> size_t {
    if (pos_ >= pattern_.size()) return make_literal('\\');
    char escaped = consume();
    if (escaped == '<') {
      Node n;
      n.kind = NodeKind::WordStart;
      return add_node(std::move(n));
    }
    if (escaped == '>') {
      Node n;
      n.kind = NodeKind::WordEnd;
      return add_node(std::move(n));
    }
    if (escaped >= '1' && escaped <= '9') {
      return make_back_reference(static_cast<size_t>(escaped - '0'));
    }
    if (auto shorthand = make_escape_class(escaped); shorthand.has_value()) {
      return *shorthand;
    }
    if (syntax_ == Syntax::Basic && escaped == '(') {
      size_t expr = parse_expression();
      if (failed()) return expr;
      if (!is_group_end_at_current()) {
        set_error("unmatched '\\(' in regular expression");
        return expr;
      }
      consume_group_end();
      return make_capture(expr);
    }
    return make_literal(escaped);
  }

  auto parse_number(size_t& cursor) const -> std::optional<int> {
    if (cursor >= pattern_.size() ||
        !std::isdigit(static_cast<unsigned char>(pattern_[cursor]))) {
      return std::nullopt;
    }
    int value = 0;
    while (cursor < pattern_.size() &&
           std::isdigit(static_cast<unsigned char>(pattern_[cursor]))) {
      int digit = pattern_[cursor] - '0';
      if (value > (std::numeric_limits<int>::max() - digit) / 10) {
        return std::nullopt;
      }
      value = value * 10 + digit;
      ++cursor;
    }
    return value;
  }

  auto parse_interval(bool escaped) -> std::optional<std::pair<int, int>> {
    size_t cursor = pos_ + (escaped ? 2 : 1);
    auto min = parse_number(cursor);
    if (!min) {
      set_error("invalid repetition count");
      return std::nullopt;
    }

    int max = *min;
    if (cursor < pattern_.size() && pattern_[cursor] == ',') {
      ++cursor;
      if (cursor < pattern_.size() &&
          std::isdigit(static_cast<unsigned char>(pattern_[cursor]))) {
        auto parsed_max = parse_number(cursor);
        if (!parsed_max) {
          set_error("invalid repetition count");
          return std::nullopt;
        }
        max = *parsed_max;
      } else {
        max = kUnlimited;
      }
    }

    bool closed = false;
    if (escaped) {
      closed = cursor + 1 < pattern_.size() && pattern_[cursor] == '\\' &&
               pattern_[cursor + 1] == '}';
      if (closed) cursor += 2;
    } else {
      closed = cursor < pattern_.size() && pattern_[cursor] == '}';
      if (closed) ++cursor;
    }

    if (!closed || !valid_range_bounds(*min, max)) {
      set_error("invalid repetition count");
      return std::nullopt;
    }

    pos_ = cursor;
    return std::pair<int, int>{*min, max};
  }

  auto parse_class_literal() -> char {
    if (peek() == '\\' && pos_ + 1 < pattern_.size()) {
      ++pos_;
      return consume();
    }
    return consume();
  }

  auto try_parse_posix_class() -> std::optional<std::string> {
    if (!(peek() == '[' && peek(1) == ':')) return std::nullopt;
    size_t name_start = pos_ + 2;
    size_t close = pattern_.find(":]", name_start);
    if (close == std::string_view::npos) return std::nullopt;
    std::string name(pattern_.substr(name_start, close - name_start));
    if (name.empty()) return std::nullopt;
    pos_ = close + 2;
    return name;
  }

  auto parse_char_class() -> size_t {
    Node n;
    n.kind = NodeKind::CharClass;
    if (consume_if('^')) n.negated = true;

    bool first = true;
    while (pos_ < pattern_.size()) {
      if (peek() == ']' && !first) {
        ++pos_;
        return add_node(std::move(n));
      }

      if (auto posix = try_parse_posix_class(); posix.has_value()) {
        n.class_atoms.push_back(
            ClassAtom{ClassAtomKind::Posix, '\0', '\0', *posix});
        first = false;
        continue;
      }

      char start = parse_class_literal();
      if (peek() == '-' && pos_ + 1 < pattern_.size() &&
          pattern_[pos_ + 1] != ']') {
        ++pos_;
        char end = parse_class_literal();
        if (ignore_case_) {
          start = fold_ascii(start);
          end = fold_ascii(end);
        }
        if (start > end) std::swap(start, end);
        n.class_atoms.push_back(
            ClassAtom{ClassAtomKind::Range, start, end, {}});
      } else {
        if (ignore_case_) start = fold_ascii(start);
        n.class_atoms.push_back(
            ClassAtom{ClassAtomKind::Literal, start, start, {}});
      }
      first = false;
    }

    set_error("unmatched '[' in regular expression");
    return make_empty();
  }
};

}  // namespace detail

class Pattern {
  struct State;

 public:
  Pattern() = default;

  [[nodiscard]] auto valid() const -> bool {
#ifdef WINUXCMD_ENABLE_PCRE2
    if (engine_ == Engine::Pcre2) return static_cast<bool>(pcre2_code_);
#endif
    return root_ != detail::kInvalidNode;
  }

  [[nodiscard]] auto capture_count() const -> size_t { return capture_count_; }

  [[nodiscard]] auto match_at(std::string_view text, size_t start) const
      -> std::optional<Match> {
    if (!valid() || start > text.size()) return std::nullopt;
#ifdef WINUXCMD_ENABLE_PCRE2
    if (engine_ == Engine::Pcre2) return pcre2_find(text, start, true);
#endif

    auto states = match_node(root_, text, make_initial_state(start));
    if (states.empty()) return std::nullopt;

    auto best = std::max_element(
        states.begin(), states.end(),
        [](const State& lhs, const State& rhs) { return lhs.pos < rhs.pos; });
    return make_match(start, *best);
  }

  [[nodiscard]] auto find_first(std::string_view text, size_t start = 0) const
      -> std::optional<Match> {
    if (!valid() || start > text.size()) return std::nullopt;
#ifdef WINUXCMD_ENABLE_PCRE2
    if (engine_ == Engine::Pcre2) return pcre2_find(text, start, false);
#endif

    for (size_t cursor = start; cursor <= text.size(); ++cursor) {
      auto states = match_node(root_, text, make_initial_state(cursor));
      if (states.empty()) continue;

      auto best = std::max_element(
          states.begin(), states.end(),
          [](const State& lhs, const State& rhs) { return lhs.pos < rhs.pos; });
      return make_match(cursor, *best);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto find_all(std::string_view text) const
      -> std::vector<Match> {
    std::vector<Match> matches;
    if (!valid()) return matches;

    size_t cursor = 0;
    while (cursor <= text.size()) {
      auto match = find_first(text, cursor);
      if (!match) break;
      cursor = match->end > match->begin ? match->end : match->begin + 1;
      matches.push_back(std::move(*match));
    }
    return matches;
  }

  [[nodiscard]] auto matches_entire(std::string_view text) const -> bool {
    if (!valid()) return false;
#ifdef WINUXCMD_ENABLE_PCRE2
    if (engine_ == Engine::Pcre2) {
      auto match = pcre2_find(text, 0, true);
      return match && match->begin == 0 && match->end == text.size();
    }
#endif
    auto states = match_node(root_, text, make_initial_state(0));
    return std::ranges::any_of(
        states, [&](const State& st) { return st.pos == text.size(); });
  }

 private:
  friend class detail::Parser;
  friend auto compile(Syntax syntax, std::string_view pattern, bool ignore_case)
      -> CompileResult;

  enum class Engine { Local, Pcre2 };

  Engine engine_ = Engine::Local;
  bool ignore_case_ = false;
  size_t root_ = detail::kInvalidNode;
  size_t capture_count_ = 0;
  std::vector<detail::Node> nodes_;
#ifdef WINUXCMD_ENABLE_PCRE2
  struct Pcre2CodeDeleter {
    auto operator()(pcre2_code* code) const -> void { pcre2_code_free(code); }
  };
  std::shared_ptr<pcre2_code> pcre2_code_;
#endif

  struct State {
    size_t pos = 0;
    std::vector<Submatch> captures;
  };

#ifdef WINUXCMD_ENABLE_PCRE2
  [[nodiscard]] auto pcre2_find(std::string_view text, size_t start,
                                bool anchored) const -> std::optional<Match> {
    if (!pcre2_code_ || start > text.size()) return std::nullopt;

    using MatchDataPtr =
        std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>;
    MatchDataPtr data(
        pcre2_match_data_create_from_pattern(pcre2_code_.get(), nullptr),
        &pcre2_match_data_free);
    if (!data) return std::nullopt;

    uint32_t options = anchored ? PCRE2_ANCHORED : 0;
    int rc = pcre2_match(pcre2_code_.get(),
                         reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(),
                         start, options, data.get(), nullptr);
    if (rc <= 0) return std::nullopt;

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data.get());
    Match match;
    match.begin = static_cast<size_t>(ovector[0]);
    match.end = static_cast<size_t>(ovector[1]);
    match.captures.resize(static_cast<size_t>(rc));
    for (int i = 0; i < rc; ++i) {
      PCRE2_SIZE begin = ovector[2 * i];
      PCRE2_SIZE end = ovector[2 * i + 1];
      if (begin == PCRE2_UNSET || end == PCRE2_UNSET) {
        match.captures[static_cast<size_t>(i)] = Submatch{};
      } else {
        match.captures[static_cast<size_t>(i)] = Submatch{
            static_cast<size_t>(begin), static_cast<size_t>(end), true};
      }
    }
    return match;
  }
#endif

  [[nodiscard]] auto make_initial_state(size_t pos) const -> State {
    return State{pos, std::vector<Submatch>(capture_count_ + 1)};
  }

  [[nodiscard]] auto make_match(size_t begin, State state) const -> Match {
    if (state.captures.size() < capture_count_ + 1) {
      state.captures.resize(capture_count_ + 1);
    }
    state.captures[0] = Submatch{begin, state.pos, true};
    return Match{begin, state.pos, std::move(state.captures)};
  }

  [[nodiscard]] auto chars_equal(char lhs, char rhs) const -> bool {
    if (!ignore_case_) return lhs == rhs;
    return detail::fold_ascii(lhs) == detail::fold_ascii(rhs);
  }

  [[nodiscard]] auto class_matches(const detail::Node& node, char ch) const
      -> bool {
    unsigned char uch = static_cast<unsigned char>(ch);
    char folded = ignore_case_ ? detail::fold_ascii(ch) : ch;
    bool matched = false;
    for (const auto& atom : node.class_atoms) {
      if (atom.kind == detail::ClassAtomKind::Literal) {
        matched = folded == atom.first;
      } else if (atom.kind == detail::ClassAtomKind::Range) {
        matched = folded >= atom.first && folded <= atom.last;
      } else {
        matched = detail::class_matches_posix(atom.name, uch);
        if (!matched && ignore_case_ && atom.name == "lower") {
          matched = std::isupper(uch) != 0;
        } else if (!matched && ignore_case_ && atom.name == "upper") {
          matched = std::islower(uch) != 0;
        }
      }
      if (matched) break;
    }
    return node.negated ? !matched : matched;
  }

  [[nodiscard]] static auto states_equal(const State& lhs, const State& rhs)
      -> bool {
    if (lhs.pos != rhs.pos || lhs.captures.size() != rhs.captures.size()) {
      return false;
    }
    for (size_t i = 0; i < lhs.captures.size(); ++i) {
      const auto& a = lhs.captures[i];
      const auto& b = rhs.captures[i];
      if (a.begin != b.begin || a.end != b.end || a.matched != b.matched) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static auto unique_states(std::vector<State> states)
      -> std::vector<State> {
    std::vector<State> out;
    out.reserve(states.size());
    for (auto& state : states) {
      if (std::ranges::none_of(out, [&](const State& seen) {
            return states_equal(state, seen);
          })) {
        out.push_back(std::move(state));
      }
    }
    return out;
  }

  [[nodiscard]] auto match_node(size_t node_id, std::string_view text,
                                const State& state) const
      -> std::vector<State> {
    if (node_id >= nodes_.size()) return {};
    const auto& node = nodes_[node_id];
    switch (node.kind) {
      case detail::NodeKind::Empty:
        return {state};
      case detail::NodeKind::Literal:
        if (state.pos < text.size() &&
            chars_equal(text[state.pos], node.literal)) {
          State next = state;
          ++next.pos;
          return {std::move(next)};
        }
        return {};
      case detail::NodeKind::Dot:
        if (state.pos < text.size()) {
          State next = state;
          ++next.pos;
          return {std::move(next)};
        }
        return {};
      case detail::NodeKind::AnchorStart:
        return state.pos == 0 ? std::vector<State>{state}
                              : std::vector<State>{};
      case detail::NodeKind::AnchorEnd:
        return state.pos == text.size() ? std::vector<State>{state}
                                        : std::vector<State>{};
      case detail::NodeKind::WordStart:
        if (state.pos >= text.size()) return {};
        if (!detail::is_word_char(
                static_cast<unsigned char>(text[state.pos]))) {
          return {};
        }
        if (state.pos > 0 && detail::is_word_char(static_cast<unsigned char>(
                                 text[state.pos - 1]))) {
          return {};
        }
        return {state};
      case detail::NodeKind::WordEnd:
        if (state.pos == 0 || !detail::is_word_char(static_cast<unsigned char>(
                                  text[state.pos - 1]))) {
          return {};
        }
        if (state.pos < text.size() &&
            detail::is_word_char(static_cast<unsigned char>(text[state.pos]))) {
          return {};
        }
        return {state};
      case detail::NodeKind::CharClass:
        if (state.pos < text.size() && class_matches(node, text[state.pos])) {
          State next = state;
          ++next.pos;
          return {std::move(next)};
        }
        return {};
      case detail::NodeKind::Sequence:
        return match_sequence(node.children, 0, text, state);
      case detail::NodeKind::Alternation: {
        std::vector<State> out;
        for (size_t child : node.children) {
          auto child_states = match_node(child, text, state);
          out.insert(out.end(), std::make_move_iterator(child_states.begin()),
                     std::make_move_iterator(child_states.end()));
        }
        return unique_states(std::move(out));
      }
      case detail::NodeKind::Capture: {
        auto states = match_node(node.child, text, state);
        for (auto& next : states) {
          if (node.capture_index >= next.captures.size()) {
            next.captures.resize(node.capture_index + 1);
          }
          next.captures[node.capture_index] =
              Submatch{state.pos, next.pos, true};
        }
        return unique_states(std::move(states));
      }
      case detail::NodeKind::BackReference: {
        if (node.capture_index >= state.captures.size()) return {};
        const auto& capture = state.captures[node.capture_index];
        if (!capture.matched) return {};
        size_t len = capture.end - capture.begin;
        if (state.pos + len > text.size()) return {};
        for (size_t i = 0; i < len; ++i) {
          if (!chars_equal(text[state.pos + i], text[capture.begin + i])) {
            return {};
          }
        }
        State next = state;
        next.pos += len;
        return {std::move(next)};
      }
      case detail::NodeKind::Repeat:
        return match_repeat(node, text, state);
    }
    return {};
  }

  [[nodiscard]] auto match_sequence(const std::vector<size_t>& children,
                                    size_t index, std::string_view text,
                                    const State& state) const
      -> std::vector<State> {
    if (index >= children.size()) return {state};

    std::vector<State> out;
    auto child_states = match_node(children[index], text, state);
    for (const auto& child_state : child_states) {
      auto tail_states = match_sequence(children, index + 1, text, child_state);
      out.insert(out.end(), std::make_move_iterator(tail_states.begin()),
                 std::make_move_iterator(tail_states.end()));
    }
    return unique_states(std::move(out));
  }

  auto collect_repeat(const detail::Node& node, std::string_view text,
                      const State& state, int count,
                      std::vector<State>& out) const -> void {
    if (node.max_repeat != detail::kUnlimited && count >= node.max_repeat) {
      if (count >= node.min_repeat) out.push_back(state);
      return;
    }

    auto child_states = match_node(node.child, text, state);
    for (const auto& next : child_states) {
      if (next.pos == state.pos) continue;
      collect_repeat(node, text, next, count + 1, out);
    }
    if (count >= node.min_repeat) out.push_back(state);
  }

  [[nodiscard]] auto match_repeat(const detail::Node& node,
                                  std::string_view text,
                                  const State& state) const
      -> std::vector<State> {
    std::vector<State> out;
    collect_repeat(node, text, state, 0, out);
    return unique_states(std::move(out));
  }
};

struct CompileResult {
  Pattern pattern;
  std::string error;

  [[nodiscard]] auto has_value() const -> bool { return error.empty(); }
  explicit operator bool() const { return has_value(); }
};

auto compile(Syntax syntax, std::string_view pattern, bool ignore_case = false)
    -> CompileResult {
#ifdef WINUXCMD_ENABLE_PCRE2
  if (syntax == Syntax::Perl) {
    int error_code = 0;
    PCRE2_SIZE error_offset = 0;
    uint32_t flags = PCRE2_DOLLAR_ENDONLY | PCRE2_UTF;
    if (ignore_case) flags |= PCRE2_CASELESS;

    using CompileContextPtr =
        std::unique_ptr<pcre2_compile_context,
                        decltype(&pcre2_compile_context_free)>;
    CompileContextPtr context(pcre2_compile_context_create(nullptr),
                              &pcre2_compile_context_free);
    if (!context) {
      return CompileResult{Pattern{}, "failed to create PCRE2 context"};
    }

#ifdef PCRE2_EXTRA_ASCII_BSD
    pcre2_set_compile_extra_options(context.get(), PCRE2_EXTRA_ASCII_BSD);
    flags |= PCRE2_UCP;
#endif

    pcre2_code* raw_code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags,
        &error_code, &error_offset, context.get());
    if (!raw_code) {
      PCRE2_UCHAR message[256]{};
      pcre2_get_error_message(error_code, message, sizeof(message));
      return CompileResult{Pattern{},
                           std::string(reinterpret_cast<const char*>(message))};
    }

    Pattern compiled;
    compiled.engine_ = Pattern::Engine::Pcre2;
    compiled.ignore_case_ = ignore_case;
    compiled.root_ = 0;
    compiled.pcre2_code_ =
        std::shared_ptr<pcre2_code>(raw_code, Pattern::Pcre2CodeDeleter{});
    return CompileResult{std::move(compiled), {}};
  }
#else
  if (syntax == Syntax::Perl) {
    return CompileResult{Pattern{},
                         "Perl matching not supported in this build"};
  }
#endif

  detail::Parser parser(syntax, pattern, ignore_case);
  size_t root = parser.parse();
  if (parser.failed()) {
    return CompileResult{Pattern{}, parser.error()};
  }

  Pattern compiled;
  compiled.ignore_case_ = parser.ignore_case();
  compiled.root_ = root;
  compiled.capture_count_ = parser.capture_count();
  compiled.nodes_ = std::move(parser).nodes();
  for (const auto& node : compiled.nodes_) {
    if (node.kind == detail::NodeKind::BackReference &&
        node.capture_index > compiled.capture_count_) {
      return CompileResult{Pattern{}, "invalid back reference"};
    }
  }
  return CompileResult{std::move(compiled), {}};
}

}  // namespace portable_regex
