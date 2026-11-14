
#include <functional>

#include "framework/winuxtest.h"

// Mirror of the recover_read_block contract in src/commands/dd.cpp: on a
// read error under conv=noerror, seek past the block; conv=sync replaces it
// with an ibs-sized pad block (' ' under block/unblock) that flows through
// the normal conversion pipeline, and a failed seek still continues but is
// reported through recovery_seek_failed.
namespace dd_pipeline {
enum class ReadBlockAction { success, pad, skip, stop };
ReadBlockAction recover_read_block(
    std::function<bool(char*, std::size_t, std::size_t&)> read,
    std::function<bool(std::size_t)> seek, bool noerror, bool sync_blocks,
    char pad_byte, std::size_t request, std::vector<char>& input_buffer,
    std::size_t& bytes_read, bool& recovery_seek_failed) {
  bytes_read = 0;
  recovery_seek_failed = false;
  if (read(input_buffer.data(), request, bytes_read)) {
    return ReadBlockAction::success;
  }
  if (!noerror) return ReadBlockAction::stop;

  const bool can_continue = seek(request);
  recovery_seek_failed = !can_continue;
  if (!sync_blocks) return ReadBlockAction::skip;
  std::fill(input_buffer.begin(), input_buffer.end(), pad_byte);
  return ReadBlockAction::pad;
}
}  // namespace dd_pipeline

TEST(dd, dd_copies_with_block_size_and_count) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghijkl");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcdefgh");
  EXPECT_TRUE(r.stderr_text.find("2+0 records in") != std::string::npos);
}

TEST(dd, dd_conv_sync_pads_short_input_blocks) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=1", L"conv=sync"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("abc\0", 4));
  EXPECT_TRUE(r.stderr_text.find("4 bytes copied") != std::string::npos);
}

TEST(dd, dd_skip_uses_input_block_size) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghi");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"ibs=3", L"obs=2",
                    L"skip=1", L"count=1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "def");
}

TEST(dd, dd_zero_count_skip_and_seek_are_allowed) {
  TempDir tmp;
  tmp.write("input.bin", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=2", L"cbs=4",
                    L"count=0", L"skip=0", L"seek=0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "");
  EXPECT_TRUE(r.stderr_text.find("0 records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("0 records out") != std::string::npos);
}

TEST(dd, dd_seek_and_notrunc_preserve_existing_bytes) {
  TempDir tmp;
  tmp.write("input.bin", "zz");
  tmp.write("out.bin", "ABCDEFGH");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"obs=4", L"seek=1", L"conv=notrunc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "ABCDzzGH");
}

TEST(dd, dd_status_none_suppresses_diagnostics) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1", L"status=none"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_status_noxfer_omits_bytes_copied_line) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1", L"status=noxfer"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("records out") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("bytes copied") == std::string::npos);
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_rejects_invalid_size_operand) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=bad"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] "dd: invalid number: 'bad'"
  EXPECT_TRUE(r.stderr_text.find("invalid number") != std::string::npos);
}

TEST(dd, dd_rejects_overflowing_size_operand) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"skip=99999999999999999999"});

  auto r = p.run();

  // [GNU] overflow reports EOVERFLOW wording and exits nonzero (#235)
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid number") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("Value too large") != std::string::npos);
}

TEST(dd, dd_iseek_is_an_alias_for_skip) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghijklmnop");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"iseek=2"});

  auto r = p.run();

  // [GNU] iseek=N is an alias for skip=N (#983)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "ijklmnop");
}

TEST(dd, dd_oseek_is_an_alias_for_seek) {
  TempDir tmp;
  tmp.write("input.bin", "abcd");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"oseek=1"});

  auto r = p.run();

  // [GNU] oseek=N is an alias for seek=N (#983): a 4-byte hole then data
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("\0\0\0\0abcd", 8));
}

TEST(dd, dd_last_skip_alias_operand_wins) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghijklmnop");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"bs=4", L"iseek=2", L"skip=3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "mnop");
}

TEST(dd, dd_zero_multiplier_warns_but_counts_zero) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"count=0x1", L"bs=1"});

  auto r = p.run();

  // [GNU] '0x' is a zero multiplier: warning plus a zero-length copy (#315)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("zero multiplier") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("0 records in") != std::string::npos);
  EXPECT_EQ(tmp.read("out.bin"), "");
}

TEST(dd, dd_zero_factor_forgives_factor_overflow) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin",
                    L"count=99999999999999999999x0", L"bs=1"});

  auto r = p.run();

  // [GNU] multiplying by zero never overflows (#377)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "");
}

TEST(dd, dd_x_factor_multiplies_block_size) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefg");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=2x3", L"count=1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcdef");
}

TEST(dd, dd_b_suffix_makes_count_a_byte_count) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghij");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=5B"});

  auto r = p.run();

  // [GNU] NB is a byte count, not a block count (coreutils >= 9.1)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcde");
}

TEST(dd, dd_iflag_byte_modes) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefghij");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"skip=2",
                    L"iflag=skip_bytes"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "cdefghij");
}

TEST(dd, dd_short_reads_do_not_corrupt_output_when_obs_gt_ibs) {
  TempDir tmp;
  tmp.write("input.bin", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"ibs=2", L"obs=4"});

  auto r = p.run();

  // [GNU] short reads accumulate into the obs buffer verbatim (#372)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcde");
}

TEST(dd, dd_seek_truncates_output_even_without_data) {
  TempDir tmp;
  tmp.write("out.bin", "ABCDEFGH");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"of=out.bin", L"seek=1", L"bs=4", L"count=0"});

  auto r = p.run();

  // [GNU] ftruncate(seek*obs) commits the size before any data is written:
  // the existing file is truncated to 4 bytes, keeping its head.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "ABCD");
}

TEST(dd, dd_skip_past_eof_warns_but_succeeds) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"skip=100", L"bs=512"});

  auto r = p.run();

  // [GNU] "cannot skip to specified offset" is a warning, not a failure
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("cannot skip to specified offset") !=
              std::string::npos);
  EXPECT_EQ(tmp.read("out.bin"), "");
}

TEST(dd, dd_dev_full_write_error_fails) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=/dev/full"});

  auto r = p.run();

  // [GNU] /dev/full fails ENOSPC and dd exits nonzero (#276)
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("No space left on device") !=
              std::string::npos);
}

TEST(dd, dd_dev_zero_reads_nul_bytes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=/dev/zero", L"of=out.bin", L"bs=4", L"count=2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("\0\0\0\0\0\0\0\0", 8));
}

TEST(dd, dd_invalid_status_level_is_quoted) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"status=foo"});

  auto r = p.run();

  // [GNU] "dd: invalid status level: 'foo'" plus the try-help hint (#470)
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid status level: 'foo'") !=
              std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("--help") != std::string::npos);
}

TEST(dd, dd_conv_excl_refuses_existing_output) {
  TempDir tmp;
  tmp.write("input.bin", "abc");
  tmp.write("out.bin", "old");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=excl"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("File exists") != std::string::npos);
  EXPECT_EQ(tmp.read("out.bin"), "old");
}

TEST(dd, dd_conv_noerror_is_accepted_with_sync) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=4", L"count=1",
                    L"conv=noerror,sync"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("abc\0", 4));
}

TEST(dd, noerror_recovery_skips_failed_block_and_syncs) {
  std::vector<char> input(4);
  std::size_t bytes_read = 0;
  bool seek_called = false;
  bool seek_failed = true;

  const auto action = dd_pipeline::recover_read_block(
      [](char*, std::size_t, std::size_t&) { return false; },
      [&](std::size_t amount) {
        seek_called = amount == 4;
        return true;
      },
      true, true, '\0', 4, input, bytes_read, seek_failed);

  // [GNU] a noerror+sync recovery emits a full ibs-sized NUL block that
  // counts as a partial input record.
  EXPECT_EQ(static_cast<int>(action),
            static_cast<int>(dd_pipeline::ReadBlockAction::pad));
  EXPECT_TRUE(seek_called);
  EXPECT_FALSE(seek_failed);
  ASSERT_EQ(input.size(), 4u);
  EXPECT_TRUE(std::all_of(input.begin(), input.end(),
                          [](char value) { return value == '\0'; }));
  EXPECT_EQ(bytes_read, 0u);
}

TEST(dd, noerror_recovery_without_sync_skips_failed_block) {
  std::vector<char> input(4);
  std::size_t bytes_read = 0;
  bool seek_failed = true;

  const auto action = dd_pipeline::recover_read_block(
      [](char*, std::size_t, std::size_t&) { return false; },
      [](std::size_t) { return true; }, true, false, '\0', 4, input, bytes_read,
      seek_failed);

  EXPECT_EQ(static_cast<int>(action),
            static_cast<int>(dd_pipeline::ReadBlockAction::skip));
  EXPECT_FALSE(seek_failed);
  EXPECT_EQ(bytes_read, 0u);
}
// #1374: dd expands the if= value in WINUX_SHELL_GLOB=native mode;
// of= names a file to create and is never expanded.
TEST(dd, dd_native_glob_expands_if_value_issue1374) {
  TempDir tmp;
  tmp.write("in.bin", "only\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUX_SHELL_GLOB", L"native");
  p.add(L"dd.exe", {L"if=in.*", L"of=out.bin", L"bs=64"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  std::ifstream out(tmp.path / "out.bin", std::ios::binary);
  std::string copied((std::istreambuf_iterator<char>(out)),
                     std::istreambuf_iterator<char>());
  EXPECT_EQ_TEXT(copied, "only\n");
}

// ---- RLIMIT_FSIZE enforcement (issue #1453) -----------------------------
// Windows has no setrlimit(); the POSIX parent declares the file-size
// limit through the environment and dd enforces it at write time with the
// GNU SIGXFSZ-death shape: silent (no diagnostic, no summary), exit
// 128+25=153, output file capped at the limit.
static std::string dd_read_file(const TempDir& tmp, const wchar_t* name) {
  std::ifstream f(tmp.path / name, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

// ulimit -f 0 through the canonical WinuxCmd contract variable.
TEST(dd, dd_rlimit_fsize_zero_kills_write_silently_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", std::string(2048, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"0");
  p.add(L"dd.exe", {L"if=in.bin", L"of=out.bin", L"bs=1024", L"count=2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 128 + 25);
  EXPECT_EQ_TEXT(r.stderr_text, std::string());
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string());
}

// The rubash ulimit emulation channel: "<soft>:<hard>" bytes, RLIM_INFINITY
// sentinel 18446744073709551615 (rubash src/builtins/ulimit.rs).
TEST(dd, dd_rubash_ulimit_f_zero_channel_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", std::string(1024, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"__RUBASH_ULIMIT_F", L"0:18446744073709551615");
  p.add(L"dd.exe", {L"if=in.bin", L"of=out.bin", L"bs=1024", L"count=1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 128 + 25);
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string());
}

// The crossing write is delivered only up to the limit (kernel shape).
TEST(dd, dd_rlimit_fsize_truncates_crossing_write_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", std::string(64, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"10");
  p.add(L"dd.exe",
        {L"if=in.bin", L"of=out.bin", L"bs=4", L"obs=4", L"count=16"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 128 + 25);
  EXPECT_EQ_TEXT(r.stderr_text, std::string());
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string(10, 'x'));
}

// A write ending exactly AT the limit is legal (RLIMIT_FSIZE fires only
// when a write would make the file exceed it).
TEST(dd, dd_rlimit_fsize_exact_write_succeeds_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", "12345678");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"8");
  p.add(L"dd.exe",
        {L"if=in.bin", L"of=out.bin", L"bs=8", L"obs=8", L"count=1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string("12345678"));
}

// No limit declared: dd writes freely (regression guard for the guard).
TEST(dd, dd_without_limit_writes_freely_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", "abcdefgh");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=in.bin", L"of=out.bin", L"bs=1024"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string("abcdefgh"));
}

// The canonical variable, literal "unlimited": enforcement disabled.
TEST(dd, dd_rlimit_fsize_unlimited_literal_writes_freely_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", "abcdefgh");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"unlimited");
  p.add(L"dd.exe", {L"if=in.bin", L"of=out.bin", L"bs=1024"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string("abcdefgh"));
}

// A malformed contract value warns once and disables enforcement.
TEST(dd, dd_rlimit_fsize_invalid_value_warns_and_disables_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", "abcdefgh");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"abc");
  p.add(L"dd.exe", {L"if=in.bin", L"of=out.bin", L"bs=1024"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid WINUXCMD_RLIMIT_FSIZE") !=
              std::string::npos);
  EXPECT_EQ_TEXT(dd_read_file(tmp, L"out.bin"), std::string("abcdefgh"));
}

// Linux scopes RLIMIT_FSIZE to regular-file writes: pipes are exempt, so
// `dd | cat` keeps working under ulimit -f.
TEST(dd, dd_rlimit_fsize_does_not_apply_to_pipes_issue1453) {
  TempDir tmp;
  tmp.write("in.bin", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_env(L"WINUXCMD_RLIMIT_FSIZE", L"0");
  // of= to stdout (a pipe under the harness) is exempt...
  p.add(L"dd.exe", {L"if=in.bin", L"bs=1", L"count=5"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, std::string("hello"));
}

// ---- issue #1707: GNU statistics shape --------------------------------

TEST(dd, dd_stats_use_full_plus_partial_shape_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=2"});

  auto r = p.run();

  // [GNU] "2+1 records in / 2+1 records out" (dd.c:828-830)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("2+1 records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("2+1 records out") != std::string::npos);
}

TEST(dd, dd_stats_line_has_time_and_rate_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin"});

  auto r = p.run();

  // [GNU] "3 bytes copied, <secs> s, <rate>/s" (dd.c:780-790)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("3 bytes copied, ") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(" s, ") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("/s") != std::string::npos);
}

TEST(dd, dd_stats_singular_byte_copied_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", "a");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("1 byte copied") != std::string::npos);
}

TEST(dd, dd_stats_human_readable_parenthesized_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", std::string(1024, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1024"});

  auto r = p.run();

  // [GNU] "1024 bytes (1.0 kB, 1.0 KiB) copied, <secs> s, <rate>"
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("1024 bytes (1.0 kB, 1.0 KiB) copied") !=
              std::string::npos);
}

TEST(dd, dd_stats_si_only_parenthesized_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", std::string(1000, 'x'));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1000"});

  auto r = p.run();

  // [GNU] below 1024 the IEC abbreviation has no prefix: "(1.0 kB)" only
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("1000 bytes (1.0 kB) copied") !=
              std::string::npos);
}

TEST(dd, dd_status_noxfer_keeps_record_shape_issue1707) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"bs=1", L"status=noxfer"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("3+0 records in") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("3+0 records out") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("bytes copied") == std::string::npos);
}

// ---- issue #1708: conv= transformations --------------------------------

TEST(dd, dd_conv_ucase_lcase_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "aBcD");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ucase"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "ABCD");
}

TEST(dd, dd_conv_lcase_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "aBcD");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=lcase"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcd");
}

TEST(dd, dd_conv_swab_pairs_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=swab"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "badcfe");
}

TEST(dd, dd_conv_swab_odd_keeps_last_byte_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abcde");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=swab"});

  auto r = p.run();

  // [GNU] the unpaired byte is written last, unswapped (dd.c:2309-2316)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "badce");
}

TEST(dd, dd_conv_swab_carries_byte_across_blocks_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abcdefg");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=swab", L"bs=4"});

  auto r = p.run();

  // [GNU] the saved byte pairs with the next block's first byte
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "badcfeg");
}

TEST(dd, dd_conv_ebcdic_translates_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ebcdic"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("\x81\x82\x83", 3));
}

TEST(dd, dd_conv_ascii_from_ebcdic_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", std::string("\x81\x82\x83", 3));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ascii"});

  auto r = p.run();

  // [GNU] EBCDIC 0x81-0x83 map back to ASCII 'a'-'c'
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_conv_block_pads_records_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "a\nbc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=block", L"cbs=4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("a   bc  ", 8));
}

TEST(dd, dd_conv_block_truncates_long_records_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abcdef");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=block", L"cbs=4"});

  auto r = p.run();

  // [GNU] bytes past cbs are dropped once per overlong record, and the
  // truncation is reported on its own line
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abcd");
  EXPECT_TRUE(r.stderr_text.find("1 truncated record") != std::string::npos);
}

TEST(dd, dd_conv_unblock_replaces_trailing_spaces_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "a   b   c   ");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=unblock", L"cbs=4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "a\nb\nc\n");
}

TEST(dd, dd_conv_ebcdic_block_uses_ebcdic_space_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "a\nb\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ebcdic", L"cbs=4"});

  auto r = p.run();

  // [GNU] the newline/space characters are the EBCDIC 0x25/0x40 after the
  // charset translation (dd.c:1717-1725)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"),
            std::string("\x81\x40\x40\x40\x82\x40\x40\x40", 8));
}

TEST(dd, dd_conv_block_without_cbs_is_a_noop_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=block,unblock"});

  auto r = p.run();

  // [GNU] cbs=0 clears block/unblock before the exclusivity check, so the
  // pair is silently dropped rather than rejected (dd.c:1609-1610)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), "abc");
}

TEST(dd, dd_conv_sync_pads_with_spaces_under_block_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "ab\nc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=sync,block", L"bs=4",
                    L"cbs=8"});

  auto r = p.run();

  // [GNU] under block the sync pad byte is a space (dd.c:2245-2248)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("ab      c       ", 16));
}

TEST(dd, dd_conv_sync_pads_to_ibs_not_cbs_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "ab");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"conv=sync", L"bs=4", L"cbs=8"});

  auto r = p.run();

  // [GNU] conv=sync pads to ibs, not cbs (dd.c:2242-2248)
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("ab\0\0", 4));
}

TEST(dd, dd_conv_combine_charset_error_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ascii,ebcdic"});

  auto r = p.run();

  // [GNU] "dd: cannot combine any two of {ascii,ebcdic,ibm}" (dd.c:1665)
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot combine any two of "
                                 "{ascii,ebcdic,ibm}") != std::string::npos);
}

TEST(dd, dd_conv_combine_block_unblock_error_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"conv=block,unblock", L"cbs=4"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot combine block and unblock") !=
              std::string::npos);
}

TEST(dd, dd_conv_combine_lcase_ucase_error_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe", {L"if=input.bin", L"of=out.bin", L"conv=ucase,lcase"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("cannot combine lcase and ucase") !=
              std::string::npos);
}

TEST(dd, dd_conv_sync_ucase_translates_pad_issue1708) {
  TempDir tmp;
  tmp.write("input.bin", "abc");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"dd.exe",
        {L"if=input.bin", L"of=out.bin", L"conv=sync,ucase", L"bs=4"});

  auto r = p.run();

  // [GNU] the NUL pad goes through the case translation like data
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("out.bin"), std::string("ABC\0", 4));
}
