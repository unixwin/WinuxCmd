// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for diff3 command.
///     Three-way file comparison following GNU diffutils 3.10: two two-way
///     diffs are merged into diff3 blocks (diff3.c make_3way_diff) and
///     rendered as the default report, an ed script (-A/-e/-E/-3/-x/-X) or
///     a merged file (-m). Output and exit statuses are byte-compatible
///     with the GNU oracle (0 ok, 1 conflicts, 2 trouble).
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <shellapi.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

#include "diff_engine.h"

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

// [GNU 3.10] wording follows `diff3 --help` verbatim.
auto constexpr DIFF3_OPTIONS = std::array{
    OPTION("-A", "--show-all", "output all changes, bracketing conflicts"),
    OPTION("-a", "--text", "treat all files as text"),
    OPTION("-e", "--ed",
           "output ed script incorporating changes from OLDFILE "
           "to YOURFILE into MYFILE"),
    OPTION("-E", "--show-overlap", "like -e, but bracket conflicts"),
    OPTION("-3", "--easy-only",
           "like -e, but incorporate only nonoverlapping changes"),
    OPTION("-x", "--overlap-only",
           "like -e, but incorporate only overlapping changes"),
    OPTION("-X", "", "like -x, but bracket conflicts"),
    OPTION("-i", "", "append 'w' and 'q' commands to ed scripts"),
    OPTION("-m", "--merge",
           "output actual merged file, according to -A if no "
           "other options are given"),
    OPTION("", "--diff-program", "use PROGRAM to compare files", STRING_TYPE),
    OPTION("", "--strip-trailing-cr",
           "strip trailing carriage return on input"),
    OPTION("-T", "--initial-tab", "make tabs line up by prepending a tab"),
    OPTION("-L", "--label",
           "use LABEL instead of file name (can be repeated up "
           "to three times)",
           STRING_TYPE)};

// ======================================================
// Two-way diff blocks (what GNU parses out of `diff` normal output)
// ======================================================

namespace diff3_pipeline {

namespace de = diff_engine;

// Ranges are 1-based inclusive; start > end denotes an empty range, the
// same convention GNU's process_diff produces for pure inserts/deletes.
struct Diff2Block {
  long fo_start = 0;
  long fo_end = 0;  // range in the pair's "other" file (< side)
  long fc_start = 0;
  long fc_end = 0;  // range in the shared file (> side)
  std::vector<std::string> fo_lines;
  std::vector<std::string> fc_lines;
};

// Build the two-way hunks directly from an edit script.
auto build_diff2_blocks(const std::vector<de::Edit> &edits,
                        const std::vector<std::string> &other,
                        const std::vector<std::string> &common)
    -> std::vector<Diff2Block> {
  std::vector<Diff2Block> blocks;
  size_t k = 0;
  while (k < edits.size()) {
    if (edits[k].type == de::EditType::KEEP) {
      ++k;
      continue;
    }
    const size_t run_start = k;
    Diff2Block block;
    bool have_fo = false;
    bool have_fc = false;
    while (k < edits.size() && edits[k].type != de::EditType::KEEP) {
      if (edits[k].type == de::EditType::DEL) {
        if (!have_fo) {
          block.fo_start = static_cast<long>(edits[k].line1_index) + 1;
          have_fo = true;
        }
        block.fo_end = static_cast<long>(edits[k].line1_index) + 1;
        block.fo_lines.push_back(other[edits[k].line1_index]);
      } else {
        if (!have_fc) {
          block.fc_start = static_cast<long>(edits[k].line2_index) + 1;
          have_fc = true;
        }
        block.fc_end = static_cast<long>(edits[k].line2_index) + 1;
        block.fc_lines.push_back(common[edits[k].line2_index]);
      }
      ++k;
    }
    if (!have_fo) {
      // Pure insertion "Na": empty range after line N of the other file.
      long anchor = static_cast<long>(edits[run_start].line1_index);
      block.fo_start = anchor + 1;
      block.fo_end = anchor;
    }
    if (!have_fc) {
      // Pure deletion "N,MdX": empty range after line X of the shared file.
      long anchor = static_cast<long>(edits[run_start].line2_index);
      block.fc_start = anchor + 1;
      block.fc_end = anchor;
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

// ======================================================
// Three-way merge of two diff threads (diff3.c make_3way_diff)
// ======================================================

enum class Diff3Type { All, First, Second, Third };

struct Diff3Block {
  Diff3Type type = Diff3Type::All;
  // Slot numbering matches GNU: FILE0 and FILE1 hold each thread's "other"
  // file, FILEC (== FILE2) holds the shared file.
  long range[3][2] = {};
  std::vector<std::string> lines[3];
};

namespace {

constexpr int kFile0 = 0;
constexpr int kFile1 = 1;
constexpr int kFileC = 2;

auto low_line(const Diff2Block &b, int slot) -> long {
  return slot == 0 ? b.fo_start : b.fc_start;
}
auto high_line(const Diff2Block &b, int slot) -> long {
  return slot == 0 ? b.fo_end : b.fc_end;
}

// using_to_diff3_block: combine one block from each thread (either side
// may contribute nothing) into one diff3 block over a shared line range.
auto using_to_diff3_block(const std::vector<Diff2Block> &t0,
                          const std::vector<Diff2Block> &t1, size_t begin0,
                          size_t end0, size_t begin1, size_t end1,
                          int low_thread, int high_thread,
                          const long last_high[3]) -> Diff3Block {
  const std::vector<Diff2Block> *threads[2] = {&t0, &t1};
  const size_t begins[2] = {begin0, begin1};
  const size_t ends[2] = {end0, end1};

  const long lowc = low_line(threads[low_thread]->at(begins[low_thread]), 1);
  const long highc =
      high_line(threads[high_thread]->at(ends[high_thread] - 1), 1);

  long low[2] = {0, 0};
  long high[2] = {0, 0};
  for (int d = 0; d < 2; ++d) {
    if (begins[d] < ends[d]) {
      const Diff2Block &first = threads[d]->at(begins[d]);
      const Diff2Block &last = threads[d]->at(ends[d] - 1);
      low[d] = lowc - low_line(first, 1) + low_line(first, 0);
      high[d] = highc - high_line(last, 1) + high_line(last, 0);
    } else {
      // The file matches the shared file over this range: map the range
      // through the previous diff3 block (zeros before the first block).
      low[d] = lowc - last_high[kFileC] + last_high[kFile0 + d];
      high[d] = highc - last_high[kFileC] + last_high[kFile0 + d];
    }
  }

  Diff3Block block;
  block.range[kFile0][0] = low[0];
  block.range[kFile0][1] = high[0];
  block.range[kFile1][0] = low[1];
  block.range[kFile1][1] = high[1];
  block.range[kFileC][0] = lowc;
  block.range[kFileC][1] = highc;
  for (int slot = 0; slot < 3; ++slot) {
    block.lines[slot].assign(
        static_cast<size_t>(block.range[slot][1] - block.range[slot][0] + 1),
        std::string());
  }

  // Shared-file lines come from each thread's stored "> " side.
  for (int d = 0; d < 2; ++d) {
    for (size_t i = begins[d]; i < ends[d]; ++i) {
      const Diff2Block &b = threads[d]->at(i);
      size_t offset = static_cast<size_t>(b.fc_start - lowc);
      for (const auto &line : b.fc_lines) {
        block.lines[kFileC][offset++] = line;
      }
    }
  }

  // Per-file lines: fill from the shared file outside each hunk, take the
  // hunk's own "< " lines inside it.
  for (int d = 0; d < 2; ++d) {
    const long lo = low[d];
    const long hi = high[d];
    auto &out = block.lines[kFile0 + d];
    const bool have_slice = begins[d] < ends[d];
    const long fill_end =
        have_slice ? low_line(threads[d]->at(begins[d]), 0) : hi + 1;
    for (long i = 0; i + lo < fill_end; ++i) {
      out[static_cast<size_t>(i)] = block.lines[kFileC][static_cast<size_t>(i)];
    }
    for (size_t idx = begins[d]; idx < ends[d]; ++idx) {
      const Diff2Block &b = threads[d]->at(idx);
      size_t offset = static_cast<size_t>(b.fo_start - lo);
      for (const auto &line : b.fo_lines) {
        out[offset++] = line;
      }
      // Lines between this hunk and the next come from the shared file.
      const bool have_next = idx + 1 < ends[d];
      const long gap_end =
          have_next ? low_line(threads[d]->at(idx + 1), 0) : hi + 1;
      long linec = b.fc_end + 1 - lowc;
      for (long i = b.fo_end + 1 - lo; i < gap_end - lo; ++i) {
        out[static_cast<size_t>(i)] =
            block.lines[kFileC][static_cast<size_t>(linec)];
        ++linec;
      }
    }
  }

  if (begins[0] >= ends[0]) {
    block.type = Diff3Type::Second;
  } else if (begins[1] >= ends[1]) {
    block.type = Diff3Type::First;
  } else {
    block.type = block.lines[kFile0] == block.lines[kFile1] ? Diff3Type::Third
                                                            : Diff3Type::All;
  }
  return block;
}

}  // namespace

auto make_3way_diff(const std::vector<Diff2Block> &t0,
                    const std::vector<Diff2Block> &t1)
    -> std::vector<Diff3Block> {
  std::vector<Diff3Block> result;
  size_t current[2] = {0, 0};
  long last_high[3] = {0, 0, 0};

  const std::vector<Diff2Block> *threads[2] = {&t0, &t1};

  while (current[0] < t0.size() || current[1] < t1.size()) {
    size_t begin[2] = {0, 0};
    size_t end[2] = {0, 0};

    int base_water_thread;
    if (current[0] >= t0.size()) {
      base_water_thread = 1;
    } else if (current[1] >= t1.size()) {
      base_water_thread = 0;
    } else {
      base_water_thread = threads[0]->at(current[0]).fc_start >
                                  threads[1]->at(current[1]).fc_start
                              ? 1
                              : 0;
    }
    int high_water_thread = base_water_thread;

    long high_water_mark =
        threads[high_water_thread]->at(current[high_water_thread]).fc_end;

    begin[high_water_thread] = current[high_water_thread];
    end[high_water_thread] = current[high_water_thread] + 1;
    current[high_water_thread] += 1;

    int other_thread = high_water_thread ^ 1;
    // Fold in every other-thread hunk that overlaps or touches the range
    // (adjacent hunks share one diff3 block, per GNU).
    while (current[other_thread] < threads[other_thread]->size() &&
           threads[other_thread]->at(current[other_thread]).fc_start <=
               high_water_mark + 1) {
      if (end[other_thread] == 0) {
        begin[other_thread] = current[other_thread];
      }
      end[other_thread] = current[other_thread] + 1;
      current[other_thread] += 1;

      const long other_high =
          threads[other_thread]->at(end[other_thread] - 1).fc_end;
      if (high_water_mark < other_high) {
        high_water_thread ^= 1;
        high_water_mark = other_high;
      }
      other_thread = high_water_thread ^ 1;
    }

    Diff3Block block =
        using_to_diff3_block(t0, t1, begin[0], end[0], begin[1], end[1],
                             base_water_thread, high_water_thread, last_high);
    for (int slot = 0; slot < 3; ++slot) {
      last_high[slot] = block.range[slot][1];
    }
    result.push_back(std::move(block));
  }
  return result;
}

// ======================================================
// Output formats
// ======================================================

namespace {

struct Diff3IO {
  std::function<void(std::string_view)> out;
  bool ends_with_newline[3] = {true, true, true};  // by arg file
  size_t file_line_count[3] = {0, 0, 0};           // by arg file
};

// GNU stores lines with their newline; a line missing it is the last line
// of a file without a trailing newline.  Emit it plus '\n', or bare when
// it is that file's unterminated final line.
void emit_block_line(const Diff3Block &block, int slot, size_t index,
                     const Diff3IO &io, int arg_file) {
  const auto &line = block.lines[slot][index];
  io.out(line);
  const bool is_file_final =
      block.range[slot][1] == static_cast<long>(io.file_line_count[arg_file]);
  if (!io.ends_with_newline[arg_file] && is_file_final &&
      index + 1 == block.lines[slot].size()) {
    return;
  }
  io.out("\n");
}

// Default report: "====[N]" then one numbered section per file; the odd
// file's content is suppressed when only one file changed.
void output_diff3(const std::vector<Diff3Block> &blocks, const int mapping[3],
                  const int rev_mapping[3], bool initial_tab,
                  const Diff3IO &io) {
  static constexpr int kSkewIncrement[3] = {2, 3, 1};  // 0==>2==>1==>3
  const char *line_prefix = initial_tab ? "\t" : "  ";

  for (const auto &block : blocks) {
    int oddoneout = 3;
    std::string tag;
    switch (block.type) {
      case Diff3Type::All:
        oddoneout = 3;
        break;
      case Diff3Type::First:
        oddoneout = rev_mapping[0];
        tag = std::string(1, static_cast<char>('1' + oddoneout));
        break;
      case Diff3Type::Second:
        oddoneout = rev_mapping[1];
        tag = std::string(1, static_cast<char>('1' + oddoneout));
        break;
      case Diff3Type::Third:
        oddoneout = rev_mapping[2];
        tag = std::string(1, static_cast<char>('1' + oddoneout));
        break;
    }
    io.out("====");
    io.out(tag);
    io.out("\n");

    // [GNU] Exactly one section may skip its content: DIFF_ALL prints all
    // (slot 3 never matches i); when MYFILE is the odd file out
    // (oddoneout 0) section 2's content is skipped, otherwise section 1's
    // is (the two agreeing files carry identical lines there).
    const int dont_print_slot =
        block.type == Diff3Type::All ? 3 : (oddoneout == 0 ? 1 : 0);
    for (int i = 0; i < 3; i = oddoneout == 1 ? kSkewIncrement[i] : i + 1) {
      const int realfile = mapping[i];
      const long lowt = block.range[realfile][0];
      const long hight = block.range[realfile][1];

      io.out(std::to_string(i + 1));
      io.out(":");
      if (lowt - hight == 1) {
        io.out(std::to_string(lowt - 1));
        io.out("a\n");
      } else if (lowt == hight) {
        io.out(std::to_string(lowt));
        io.out("c\n");
      } else {
        io.out(std::to_string(lowt));
        io.out(",");
        io.out(std::to_string(hight));
        io.out("c\n");
      }

      if (i == dont_print_slot) {
        continue;
      }

      if (lowt <= hight) {
        const size_t count = block.lines[realfile].size();
        for (size_t line = 0; line < count; ++line) {
          io.out(line_prefix);
          const auto &text = block.lines[realfile][line];
          io.out(text);
          const bool is_file_final =
              hight == static_cast<long>(io.file_line_count[mapping[i]]) &&
              !io.ends_with_newline[mapping[i]];
          if (!(is_file_final && line + 1 == count)) {
            io.out("\n");
          } else {
            io.out("\n\\ No newline at end of file\n");
          }
        }
      }
    }
  }
}

// ed script body helpers: double leading dots so ed never eats a command.
bool dotlines(const Diff3Block &block, int slot, const Diff3IO &io) {
  bool leading_dot = false;
  for (const auto &line : block.lines[slot]) {
    if (!line.empty() && line[0] == '.') {
      leading_dot = true;
      io.out(".");
    }
    io.out(line);
    io.out("\n");
  }
  return leading_dot;
}

void undotlines(bool leading_dot, long start, long num, const Diff3IO &io) {
  io.out(".\n");
  if (leading_dot) {
    if (num == 1) {
      io.out(std::to_string(start));
      io.out("s/^\\.//\n");
    } else {
      io.out(std::to_string(start));
      io.out(",");
      io.out(std::to_string(start + num - 1));
      io.out("s/^\\.//\n");
    }
  }
}

// Which conflict classes this script renders (diff3.c output_diff3_edscript
// and output_diff3_merge share the selection logic).
struct RenderPolicy {
  bool show_2nd = false;
  bool simple_only = false;
  bool overlap_only = false;
  bool flagging = false;
};

// Returns nullopt when the block is skipped for this mode; otherwise the
// pair (remapped type, is_conflict).
auto classify_block(const Diff3Block &block, const int rev_mapping[3],
                    const RenderPolicy &policy)
    -> std::optional<std::pair<Diff3Type, bool>> {
  Diff3Type type = block.type;
  if (type != Diff3Type::All) {
    type =
        static_cast<Diff3Type>(static_cast<int>(Diff3Type::First) +
                               rev_mapping[static_cast<int>(block.type) -
                                           static_cast<int>(Diff3Type::First)]);
  }
  switch (type) {
    case Diff3Type::Second:
      if (!policy.show_2nd) return std::nullopt;
      return std::make_pair(type, true);
    case Diff3Type::Third:
      if (policy.overlap_only) return std::nullopt;
      return std::make_pair(type, false);
    case Diff3Type::All:
      if (policy.simple_only) return std::nullopt;
      return std::make_pair(type, policy.flagging);
    case Diff3Type::First:
      break;
  }
  return std::nullopt;
}

// ed script (-A/-e/-E/-3/-x/-X without -m).  Commands come out in reverse
// block order so earlier line numbers stay valid while ed applies them.
auto output_diff3_edscript(const std::vector<Diff3Block> &blocks,
                           const int mapping[3], const int rev_mapping[3],
                           const std::array<std::string, 3> &tags,
                           const RenderPolicy &policy, bool final_write,
                           const Diff3IO &io) -> bool {
  bool conflicts_found = false;
  for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
    const Diff3Block &block = *it;
    auto classified = classify_block(block, rev_mapping, policy);
    if (!classified) continue;
    const auto [type, conflict] = *classified;

    const long low0 = block.range[mapping[0]][0];
    const long high0 = block.range[mapping[0]][1];

    if (conflict) {
      conflicts_found = true;

      // Mark end of conflict.
      io.out(std::to_string(high0));
      io.out("a\n");
      bool leading_dot = false;
      if (type == Diff3Type::All) {
        if (policy.show_2nd) {
          io.out("||||||| ");
          io.out(tags[1]);
          io.out("\n");
          leading_dot = dotlines(block, mapping[1], io);
        }
        io.out("=======\n");
        leading_dot = dotlines(block, mapping[2], io) || leading_dot;
      }
      io.out(">>>>>>> ");
      io.out(tags[2]);
      io.out("\n");
      undotlines(leading_dot, high0 + 2,
                 static_cast<long>(block.lines[mapping[1]].size() +
                                   block.lines[mapping[2]].size() + 1),
                 io);

      // Mark start of conflict.
      io.out(std::to_string(low0 - 1));
      io.out("a\n<<<<<<< ");
      io.out(type == Diff3Type::All ? tags[0] : tags[1]);
      io.out("\n");
      leading_dot = false;
      if (type == Diff3Type::Second) {
        leading_dot = dotlines(block, mapping[1], io);
        io.out("=======\n");
      }
      undotlines(leading_dot, low0 + 1,
                 static_cast<long>(block.lines[mapping[1]].size()), io);
    } else if (block.lines[mapping[2]].empty()) {
      // Delete
      if (low0 == high0) {
        io.out(std::to_string(low0));
        io.out("d\n");
      } else {
        io.out(std::to_string(low0));
        io.out(",");
        io.out(std::to_string(high0));
        io.out("d\n");
      }
    } else {
      // Add or change
      switch (high0 - low0) {
        case -1:
          io.out(std::to_string(high0));
          io.out("a\n");
          break;
        case 0:
          io.out(std::to_string(high0));
          io.out("c\n");
          break;
        default:
          io.out(std::to_string(low0));
          io.out(",");
          io.out(std::to_string(high0));
          io.out("c\n");
          break;
      }
      bool leading_dot = dotlines(block, mapping[2], io);
      undotlines(leading_dot, low0,
                 static_cast<long>(block.lines[mapping[2]].size()), io);
    }
  }
  if (final_write) {
    io.out("w\nq\n");
  }
  return conflicts_found;
}

// Merged file (-m).  Everything outside blocks is copied from MYFILE
// verbatim; block interiors render per the policy.
auto output_diff3_merge(const std::vector<Diff3Block> &blocks,
                        const std::vector<std::string> &mine_lines,
                        const int mapping[3], const int rev_mapping[3],
                        const std::array<std::string, 3> &tags,
                        const RenderPolicy &policy, const Diff3IO &io) -> bool {
  bool conflicts_found = false;
  size_t lines_read = 0;

  for (const auto &block : blocks) {
    auto classified = classify_block(block, rev_mapping, policy);
    if (!classified) continue;
    const auto [type, conflict] = *classified;

    // Copy the untouched MYFILE lines ahead of the block.
    const long ahead = block.range[0][0] - static_cast<long>(lines_read) - 1;
    for (long i = 0; i < ahead; ++i) {
      const size_t idx = lines_read + static_cast<size_t>(i);
      io.out(mine_lines[idx]);
      const bool is_file_final =
          idx + 1 == mine_lines.size() && !io.ends_with_newline[0];
      io.out(is_file_final ? "" : "\n");
    }
    lines_read += static_cast<size_t>(ahead);

    if (conflict) {
      conflicts_found = true;
      if (type == Diff3Type::All) {
        io.out("<<<<<<< ");
        io.out(tags[0]);
        io.out("\n");
        const auto &mine_block = block.lines[mapping[0]];
        for (size_t i = 0; i < mine_block.size(); ++i) {
          emit_block_line(block, mapping[0], i, io, 0);
        }
      }
      if (policy.show_2nd) {
        io.out(type == Diff3Type::All ? "||||||| " : "<<<<<<< ");
        io.out(tags[1]);
        io.out("\n");
        const auto &lines1 = block.lines[mapping[1]];
        for (size_t i = 0; i < lines1.size(); ++i) {
          emit_block_line(block, mapping[1], i, io, rev_mapping[1]);
        }
      }
      io.out("=======\n");
    }

    const auto &lines2 = block.lines[mapping[2]];
    for (size_t i = 0; i < lines2.size(); ++i) {
      emit_block_line(block, mapping[2], i, io, rev_mapping[2]);
    }

    if (conflict) {
      io.out(">>>>>>> ");
      io.out(tags[2]);
      io.out("\n");
    }

    // Skip the block's MYFILE lines.
    lines_read += block.lines[0].size();
  }

  // Copy the rest of MYFILE.
  for (size_t idx = lines_read; idx < mine_lines.size(); ++idx) {
    io.out(mine_lines[idx]);
    const bool is_file_final =
        idx + 1 == mine_lines.size() && !io.ends_with_newline[0];
    io.out(is_file_final ? "" : "\n");
  }
  return conflicts_found;
}

}  // namespace

// ======================================================
// Subsidiary diff (--diff-program=PROGRAM) and normal-diff parsing
// ======================================================

namespace {

auto parse_normal_diff(const std::string &text, std::vector<Diff2Block> &blocks)
    -> bool {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t pos = text.find('\n', start);
    if (pos == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, pos - start));
    start = pos + 1;
  }

  size_t i = 0;
  auto read_num = [&](const std::string &s, size_t &p, long &out) -> bool {
    if (p >= s.size() || s[p] < '0' || s[p] > '9') return false;
    long value = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
      value = value * 10 + (s[p] - '0');
      ++p;
    }
    out = value;
    return true;
  };

  while (i < lines.size()) {
    const std::string &header = lines[i];
    size_t p = 0;
    long a0 = 0;
    long a1 = -1;
    char cmd = 0;
    long b0 = 0;
    long b1 = -1;
    if (!read_num(header, p, a0)) return false;
    if (p < header.size() && header[p] == ',') {
      ++p;
      if (!read_num(header, p, a1)) return false;
    }
    if (p >= header.size()) return false;
    cmd = header[p++];
    if (cmd != 'a' && cmd != 'c' && cmd != 'd') return false;
    if (!read_num(header, p, b0)) return false;
    if (p < header.size() && header[p] == ',') {
      ++p;
      if (!read_num(header, p, b1)) return false;
    }
    if (p != header.size()) return false;
    ++i;
    if (a1 < 0) a1 = a0;
    if (b1 < 0) b1 = b0;

    Diff2Block block;
    switch (cmd) {
      case 'a':
        block.fo_start = a0 + 1;
        block.fo_end = a0;
        block.fc_start = b0;
        block.fc_end = b1;
        break;
      case 'd':
        block.fo_start = a0;
        block.fo_end = a1;
        block.fc_start = b0 + 1;
        block.fc_end = b0;
        break;
      default:  // 'c'
        block.fo_start = a0;
        block.fo_end = a1;
        block.fc_start = b0;
        block.fc_end = b1;
        break;
    }

    if (cmd != 'a') {
      while (i < lines.size() && lines[i].rfind("< ", 0) == 0) {
        block.fo_lines.push_back(lines[i].substr(2));
        ++i;
      }
    }
    if (cmd == 'c') {
      if (i >= lines.size() || lines[i] != "---") return false;
      ++i;
    }
    if (cmd != 'd') {
      while (i < lines.size() && lines[i].rfind("> ", 0) == 0) {
        block.fc_lines.push_back(lines[i].substr(2));
        ++i;
      }
    }
    blocks.push_back(std::move(block));
  }
  return true;
}

// Run PROGRAM the way GNU read_diff does and parse its normal output.
auto read_diff_via_program(const std::string &program, bool text_mode,
                           bool strip_cr, const std::string &filea,
                           const std::string &fileb,
                           std::vector<Diff2Block> &blocks) -> bool {
  std::string cmdline = "\"" + program + "\"";
  if (text_mode) cmdline += " -a";
  if (strip_cr) cmdline += " --strip-trailing-cr";
  cmdline += " --horizon-lines=100 ---no-directory -- \"" + filea + "\" \"" +
             fileb + "\"";

  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
    return false;
  }
  HANDLE err_write = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE),
                       GetCurrentProcess(), &err_write, 0, TRUE,
                       DUPLICATE_SAME_ACCESS)) {
    CloseHandle(read_end);
    CloseHandle(write_end);
    return false;
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_end;
  si.hStdError = err_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  const BOOL spawned = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr,
                                      TRUE, 0, nullptr, nullptr, &si, &pi);
  if (!spawned) {
    CloseHandle(read_end);
    CloseHandle(write_end);
    CloseHandle(err_write);
    safeErrorPrint("diff3: subsidiary program '");
    safeErrorPrint(program);
    safeErrorPrintLn("' not found");
    return false;
  }
  CloseHandle(write_end);
  CloseHandle(err_write);

  std::string output;
  std::array<char, 4096> buffer{};
  DWORD read = 0;
  while (ReadFile(read_end, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr) &&
         read > 0) {
    output.append(buffer.data(), read);
  }
  CloseHandle(read_end);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (code > 1) {
    safeErrorPrint("diff3: subsidiary program '");
    safeErrorPrint(program);
    safeErrorPrint("' failed (exit status ");
    safeErrorPrintLn(std::to_string(code) + ")");
    return false;
  }
  if (!parse_normal_diff(output, blocks)) {
    safeErrorPrintLn("diff3: diff failed: invalid diff output");
    return false;
  }
  return true;
}

}  // namespace

// ======================================================
// Pipeline components
// ======================================================
namespace cp = core::pipeline;

struct Diff3Config {
  bool show_2nd = false;
  bool flagging = false;
  bool simple_only = false;
  bool overlap_only = false;
  bool final_write = false;
  bool merge = false;
  bool edscript = false;
  bool text_mode = false;
  bool strip_trailing_cr = false;
  bool initial_tab = false;
  std::string diff_program;
  std::array<std::string, 3> tags;
  std::array<std::string, 3> files;
};

auto resolve_files(const CommandContext<DIFF3_OPTIONS.size()> &ctx)
    -> cp::Result<std::array<std::string, 3>> {
  std::vector<std::string> files;

  for (const auto &positional : ctx.positionals) {
    std::string file_arg = std::string(positional);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        for (const auto &file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    files.push_back(file_arg);
  }

  if (files.empty()) {
    return std::unexpected("missing operand");
  }
  if (files.size() < 3) {
    return std::unexpected("missing operand after '" + files.back() + "'");
  }
  if (files.size() > 3) {
    return std::unexpected("extra operand '" + files[3] + "'");
  }

  return std::array<std::string, 3>{files[0], files[1], files[2]};
}

}  // namespace diff3_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(diff3,
                 /* name */
                 "diff3",

                 /* synopsis */
                 "diff3 [OPTION]... MYFILE OLDFILE YOURFILE",

                 /* description */
                 "Compare three files line by line.\n",

                 /* examples */
                 "  diff3 -m mine.txt older.txt yours.txt    Merge with "
                 "conflict brackets\n"
                 "  diff3 -A mine older yours    ed script bracketing "
                 "conflicts\n"
                 "  diff3 mine older yours    Default change report",

                 /* see_also */
                 "diff(1), sdiff(1), patch(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 DIFF3_OPTIONS) {
  using namespace diff3_pipeline;
  using namespace core::pipeline;

#ifdef _WIN32
  // [GNU] "-" reads raw stdin bytes; text mode would strip CR.
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  auto try_help = [](const std::string &message) -> int {
    safeErrorPrint("diff3: ");
    safeErrorPrintLn(message);
    safeErrorPrint("diff3: Try 'diff3 --help' for more information.\n");
    return 2;
  };

  // Last raw argument, as GNU reports in "missing operand after '%s'".
  auto last_raw_argument = []() -> std::string {
    int argc = 0;
    LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv_w || argc < 1) return "diff3";
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i) {
      std::string token = wstring_to_utf8(argv_w[i]);
      if (i == 1 && token == "diff3") continue;
      tokens.push_back(token);
    }
    LocalFree(argv_w);
    if (tokens.empty()) return "diff3";
    return tokens.back();
  };

  // -- Option flags (GNU diff3.c main) --
  enum : unsigned {
    kOptA = 1u,
    kOptx = 1u << 1,
    kOptX = 1u << 2,
    kOptE = 1u << 3,
    kOpte = 1u << 4,
    kOpt3 = 1u << 5
  };
  unsigned incompat = 0;
  Diff3Config cfg;

  if (ctx.has("-A") || ctx.has("--show-all")) {
    cfg.show_2nd = true;
    cfg.flagging = true;
    incompat |= kOptA;
  }
  if (ctx.has("-x") || ctx.has("--overlap-only")) {
    cfg.overlap_only = true;
    incompat |= kOptx;
  }
  if (ctx.has("-3") || ctx.has("--easy-only")) {
    cfg.simple_only = true;
    incompat |= kOpt3;
  }
  if (ctx.has("-X")) {
    cfg.overlap_only = true;
    incompat |= kOptX;
  }
  if (ctx.has("-E") || ctx.has("--show-overlap")) {
    cfg.flagging = true;
    incompat |= kOptE;
  }
  if (ctx.has("-e") || ctx.has("--ed")) {
    incompat |= kOpte;
  }
  if (ctx.has("-i")) cfg.final_write = true;
  if (ctx.has("-m") || ctx.has("--merge")) cfg.merge = true;
  cfg.text_mode = ctx.has("-a") || ctx.has("--text");
  cfg.strip_trailing_cr = ctx.has("--strip-trailing-cr");
  cfg.initial_tab = ctx.has("-T") || ctx.has("--initial-tab");
  cfg.diff_program = ctx.get<std::string>("--diff-program", "");

  std::vector<std::string> labels;
  for (const auto &occ : ctx.string_occurrences({"-L", "--label"})) {
    labels.push_back(std::string(occ.value));
  }
  if (labels.size() > 3) {
    return try_help("too many file label options");
  }

  // -AeExX3 without -m implies an ed script; bare -m implies -A.
  cfg.edscript = incompat != 0 && !cfg.merge;
  if (incompat == 0 && cfg.merge) {
    cfg.show_2nd = true;
    cfg.flagging = true;
  }

  const unsigned compat_bits = incompat - (incompat & (incompat - 1));
  if (incompat != 0 && compat_bits != incompat) {
    return try_help("incompatible options");
  }
  if (cfg.final_write && cfg.merge) {
    return try_help("incompatible options");
  }
  if (!labels.empty() && !cfg.flagging) {
    return try_help("incompatible options");
  }

  auto files_result = resolve_files(ctx);
  if (!files_result) {
    const std::string &error = files_result.error();
    // [GNU] "missing operand after '%s'" names the last raw argument,
    // option or not; zero operands name the program itself.
    if (error.rfind("missing operand", 0) == 0) {
      return try_help("missing operand after '" + last_raw_argument() + "'");
    }
    return try_help(error);
  }

  cfg.files = *files_result;

  // '-' handling: the common file must not be standard input twice.
  int common = 2 - (cfg.edscript || cfg.merge ? 1 : 0);
  if (cfg.files[common] == "-") {
    common = 3 - common;
    if (cfg.files[0] == "-" || cfg.files[common] == "-") {
      return try_help("'-' specified for more than one input file");
    }
  }
  int mapping[3] = {0, 3 - common, common};
  int rev_mapping[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    rev_mapping[mapping[i]] = i;
  }

  for (size_t i = labels.size(); i < 3; ++i) {
    cfg.tags[i] = cfg.files[i];
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    cfg.tags[i] = labels[i];
  }

  // -- Read inputs --
  std::array<std::optional<de::FileLines>, 3> inputs;
  auto read_input = [&](size_t index) -> bool {
    auto result = de::read_file_lines_result(cfg.files[index]);
    if (!result) {
      safeErrorPrint("diff: ");
      safeErrorPrintLn(result.error());
      return false;
    }
    inputs[index] = std::move(result.value());
    return true;
  };

  // GNU runs the thread1 pair first, then thread0; each subsidiary diff
  // reports its unreadable operands, then exits with trouble.  A pair
  // mixing a directory with a regular file prints the directory mismatch
  // on stdout (exit 1), which the diff3 parser then rejects.
  auto check_pair = [&](int filea, int fileb) -> bool {
    bool missing = false;
    for (int arg : {filea, fileb}) {
      if (de::operand_is_stdin(cfg.files[arg])) continue;
      auto operand = native_path::make_api_path_operand(cfg.files[arg]);
      if (native_path::operand_target_attributes_w(operand) ==
          INVALID_FILE_ATTRIBUTES) {
        safeErrorPrint("diff: ");
        safeErrorPrint(cfg.files[arg]);
        safeErrorPrintLn(": No such file or directory");
        missing = true;
      }
    }
    if (missing) {
      safeErrorPrintLn(
          "diff3: subsidiary program 'diff' failed (exit status 2)");
      return false;
    }
    const bool dir_a = de::operand_target_is_directory(cfg.files[filea]);
    const bool dir_b = de::operand_target_is_directory(cfg.files[fileb]);
    if (dir_a != dir_b) {
      const int dir_arg = dir_a ? filea : fileb;
      const int other_arg = dir_a ? fileb : filea;
      safeErrorPrint("diff3: diff failed: File ");
      safeErrorPrint(cfg.files[dir_arg]);
      safeErrorPrint(" is a directory while file ");
      safeErrorPrint(cfg.files[other_arg]);
      safeErrorPrintLn(dir_b ? " is a directory" : " is a regular file");
      return false;
    }
    return true;
  };

  if (!check_pair(mapping[1], mapping[2])) {
    return 2;
  }
  if (!check_pair(mapping[0], mapping[2])) {
    return 2;
  }
  for (size_t index = 0; index < 3; ++index) {
    if (!read_input(index)) {
      safeErrorPrintLn(
          "diff3: subsidiary program 'diff' failed (exit status 2)");
      return 2;
    }
  }

  // -- Build the two threads on the chosen common file --
  auto normalize = [&](const de::FileLines &input) {
    std::vector<std::string> lines = input.lines;
    if (cfg.strip_trailing_cr) {
      lines = de::strip_trailing_cr_lines(lines);
    }
    return lines;
  };

  const std::vector<std::string> file_texts[3] = {
      normalize(*inputs[0]), normalize(*inputs[1]), normalize(*inputs[2])};

  auto make_thread =
      [&](int filea_arg,
          int fileb_arg) -> std::optional<std::vector<Diff2Block>> {
    const std::vector<std::string> &other = file_texts[filea_arg];
    const std::vector<std::string> &shared = file_texts[fileb_arg];
    if (!cfg.diff_program.empty()) {
      std::vector<Diff2Block> blocks;
      if (!read_diff_via_program(cfg.diff_program, cfg.text_mode,
                                 cfg.strip_trailing_cr, cfg.files[filea_arg],
                                 cfg.files[fileb_arg], blocks)) {
        return std::nullopt;
      }
      return blocks;
    }
    // [GNU io.c] An incomplete last line only matches the other side's
    // incomplete last line; diff3 has no trailing-space options.
    auto marked_other = other;
    auto marked_shared = shared;
    de::mark_incomplete_last_line(marked_other,
                                  inputs[filea_arg]->ends_with_newline, false);
    de::mark_incomplete_last_line(marked_shared,
                                  inputs[fileb_arg]->ends_with_newline, false);
    auto edits = de::compute_diff(marked_other, marked_shared);
    return build_diff2_blocks(edits, other, shared);
  };

  auto thread1 = make_thread(mapping[1], mapping[2]);
  if (!thread1) return 2;
  auto thread0 = make_thread(mapping[0], mapping[2]);
  if (!thread0) return 2;

  std::vector<Diff3Block> blocks = make_3way_diff(*thread0, *thread1);

  Diff3IO io;
  io.out = [](std::string_view text) { safePrint(text); };
  for (size_t arg = 0; arg < 3; ++arg) {
    io.ends_with_newline[arg] = inputs[arg]->ends_with_newline;
    io.file_line_count[arg] = file_texts[arg].size();
  }

  RenderPolicy policy;
  policy.show_2nd = cfg.show_2nd;
  policy.flagging = cfg.flagging;
  policy.simple_only = cfg.simple_only;
  policy.overlap_only = cfg.overlap_only;

  if (cfg.edscript) {
    bool conflicts = output_diff3_edscript(
        blocks, mapping, rev_mapping, cfg.tags, policy, cfg.final_write, io);
    return conflicts ? 1 : 0;
  }
  if (cfg.merge) {
    // The merge copies MYFILE verbatim (GNU re-reads the raw file), so it
    // gets the un-stripped lines while blocks carry the normalized ones.
    bool conflicts = output_diff3_merge(blocks, inputs[0]->lines, mapping,
                                        rev_mapping, cfg.tags, policy, io);
    return conflicts ? 1 : 0;
  }
  output_diff3(blocks, mapping, rev_mapping, cfg.initial_tab, io);
  return 0;
}
