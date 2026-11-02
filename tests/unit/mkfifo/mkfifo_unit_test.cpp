#include "framework/winuxtest.h"

TEST(mkfifo, mkfifo_missing_operand) {
  Pipeline p;
  p.add(L"mkfifo.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("missing operand"), std::string::npos);
}

TEST(mkfifo, mkfifo_invalid_mode_fails) {
  Pipeline p;
  p.add(L"mkfifo.exe", {L"-m", L"bad!mode", L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("invalid mode"), std::string::npos);
}

TEST(mkfifo, mkfifo_existing_path_fails_like_gnu_shape) {
  TempDir tmp;
  tmp.write("pipe1", "already here");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
}

TEST(mkfifo, mkfifo_creates_fifo_marker) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("pipe1"), "!<fifo>");
  EXPECT_NE(tmp.attrs("pipe1") & FILE_ATTRIBUTE_SYSTEM, 0u);
}

TEST(mkfifo, mkfifo_marker_is_seen_as_fifo) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1"});
  ASSERT_EQ(p.run().exit_code, 0);

  Pipeline t;
  t.set_cwd(tmp.wpath());
  t.add(L"test.exe", {L"-p", L"pipe1"});
  EXPECT_EQ(t.run().exit_code, 0);

  Pipeline tf;
  tf.set_cwd(tmp.wpath());
  tf.add(L"test.exe", {L"-f", L"pipe1"});
  EXPECT_EQ(tf.run().exit_code, 1);

  Pipeline s;
  s.set_cwd(tmp.wpath());
  s.add(L"stat.exe", {L"-c", L"%F", L"pipe1"});
  auto sr = s.run();
  EXPECT_EQ(sr.exit_code, 0);
  EXPECT_NE(sr.stdout_text.find("fifo"), std::string::npos);
}

TEST(mkfifo, mkfifo_multiple_operands_create_each) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"pipe1", L"pipe2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(tmp.read("pipe1"), "!<fifo>");
  EXPECT_EQ(tmp.read("pipe2"), "!<fifo>");
}

TEST(mkfifo, mkfifo_mode_umask_applies_readonly) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {L"-m", L"444", L"pipe1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(tmp.attrs("pipe1") & FILE_ATTRIBUTE_READONLY, 0u);
}

// ===== #1235 regression: POSIX drive-dialect operands =====
// mkfifo handed the raw operand ('/e/p', '/cygdrive/e/p', '/mnt/e/p') to the
// existence probe and create_winux_fifo_w. Win32 resolves a leading "/e"
// against the CURRENT drive, so from a cwd on another drive the probe
// answered for "<cwd>:\e\p" (ENOENT) and the marker would have landed beside
// the cwd. The probes below synthesize the dialect spelling from the
// TempDir's own drive, so they pass on any host.
namespace mkfifo_dialect_detail {

// Lowercase drive letter of an absolute path ("C:\x" -> L'c').
auto drive_letter(const std::filesystem::path &path) -> wchar_t {
  wchar_t drive = path.wstring()[0];
  if (drive >= L'A' && drive <= L'Z') {
    drive = static_cast<wchar_t>(drive - L'A' + L'a');
  }
  return drive;
}

// "C:\Users\...\wct123" -> "/c/Users/.../wct123" (MSYS drive spelling).
auto slash_drive_form(const std::filesystem::path &path) -> std::wstring {
  std::wstring text = path.wstring();
  text[0] = drive_letter(path);
  text.erase(1, 1);  // drop the ':' from "c:\..." (MSYS spelling has none)
  for (wchar_t &ch : text) {
    if (ch == L'\\') ch = L'/';
  }
  return L"/" + text;
}

// First fixed drive whose letter differs from OTHER, or L'\0' when none.
auto other_fixed_drive(wchar_t other) -> wchar_t {
  const DWORD mask = GetLogicalDrives();
  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
    if ((mask & (1u << (letter - L'A'))) == 0) continue;
    if (letter == other) continue;
    if (GetDriveTypeW((std::wstring(1, letter) + L":\\").c_str()) !=
        DRIVE_FIXED) {
      continue;
    }
    return letter;
  }
  return L'\0';
}

// Where the BUGGY code put the probe leaf instead: rooted on the cwd drive,
// "<cwd>:\e\<tmp-rest>\probe1235".
auto shadow_leaf(const TempDir &tmp, wchar_t cwd_drive, const wchar_t *leaf)
    -> std::filesystem::path {
  const wchar_t operand_drive = drive_letter(tmp.path);
  std::wstring text(1, cwd_drive);
  text += L":\\";
  text += operand_drive;
  text += L"\\";
  text += tmp.path.wstring().substr(3);  // past "X:\"
  return std::filesystem::path(text) / leaf;
}

}  // namespace mkfifo_dialect_detail

TEST(mkfifo, mkfifo_drive_dialect_operand_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t tmp_drive = mkfifo_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand =
      mkfifo_dialect_detail::slash_drive_form(tmp.path / L"probe1235");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkfifo.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkfifo stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  // Landing: the marker file under the TempDir, on the operand's drive.
  EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  EXPECT_NE(tmp.attrs("probe1235") & FILE_ATTRIBUTE_SYSTEM, 0u);
  // Shadow negative (#1235): the unfolded operand rooted the leaf on the cwd
  // drive ("<cwd>:\e\<tmp-rest>") - that leaf must never exist.
  EXPECT_FALSE(std::filesystem::exists(
      mkfifo_dialect_detail::shadow_leaf(tmp, tmp_drive, L"probe1235")));
}

TEST(mkfifo, mkfifo_drive_dialect_cwd_on_other_drive_lands_on_operand_drive) {
  TempDir tmp;
  const wchar_t other = mkfifo_dialect_detail::other_fixed_drive(
      mkfifo_dialect_detail::drive_letter(tmp.path));
  if (other == L'\0') {
    std::cout << "  SKIPPED (no second fixed drive to host the cwd)\n";
    return;
  }
  const std::wstring operand =
      mkfifo_dialect_detail::slash_drive_form(tmp.path / L"probe1235");

  Pipeline p;
  p.set_cwd(std::wstring(1, other) + L":\\");
  p.add(L"mkfifo.exe", {operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkfifo.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkfifo stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  // The audit's exact shadow shape: cwd on one drive, operand on another -
  // the unfolded spelling wrote "<cwd>:\e\..." on the cwd drive.
  EXPECT_FALSE(std::filesystem::exists(
      mkfifo_dialect_detail::shadow_leaf(tmp, other, L"probe1235")));
}

TEST(mkfifo, mkfifo_drive_dialect_cygdrive_and_mnt_prefixes_lands) {
  TempDir tmp;
  const std::wstring slash =
      mkfifo_dialect_detail::slash_drive_form(tmp.path / L"probe1235");
  // "/cygdrive/c/..." and "/mnt/c/..." fold onto the same file (#1145/#1172).
  const std::wstring cyg = L"/cygdrive" + slash;
  const std::wstring mnt = L"/mnt" + slash;

  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"mkfifo.exe", {cyg});
    auto r = p.run();
    TEST_LOG_CMD_LIST("mkfifo.exe", cyg);
    TEST_LOG_EXIT_CODE(r);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.empty());
    EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  }
  std::filesystem::remove(tmp.path / L"probe1235");
  {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"mkfifo.exe", {mnt});
    auto r = p.run();
    TEST_LOG_CMD_LIST("mkfifo.exe", mnt);
    TEST_LOG_EXIT_CODE(r);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.empty());
    EXPECT_EQ(tmp.read("probe1235"), "!<fifo>");
  }
}

TEST(mkfifo, mkfifo_bare_drive_root_operand_reports_file_exists) {
  TempDir tmp;
  const wchar_t drive = mkfifo_dialect_detail::drive_letter(tmp.path);
  const std::wstring operand = std::wstring(L"/") + drive;
  // What the buggy code did here: created a marker FILE at "<drive>:\<drive>"
  // when the cwd sat elsewhere. Only assert absence when it does not
  // pre-exist.
  const std::filesystem::path shadow_root =
      std::filesystem::path(std::wstring(1, drive) + L":\\" + drive);
  const bool shadow_pre_existed = std::filesystem::exists(shadow_root);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkfifo.exe", operand);
  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mkfifo stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_NE(r.stderr_text.find("File exists"), std::string::npos);
  if (!shadow_pre_existed) {
    EXPECT_FALSE(std::filesystem::exists(shadow_root));
  }
}

TEST(mkfifo, mkfifo_drive_dialect_existing_target_quotes_operand_spelling) {
  TempDir tmp;
  tmp.write("pipe1235", "already here");
  const std::wstring operand =
      mkfifo_dialect_detail::slash_drive_form(tmp.path / L"pipe1235");
  const std::string operand_utf8(operand.begin(), operand.end());

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"mkfifo.exe", {operand});

  auto r = p.run();

  TEST_LOG_CMD_LIST("mkfifo.exe", operand);
  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
  // The message quotes the spelling the user handed over, never the folded
  // "X:\..." API form (#1243 display precedent).
  EXPECT_NE(r.stderr_text.find("cannot create fifo '" + operand_utf8 + "'"),
            std::string::npos);
  EXPECT_EQ(r.stderr_text.find("X:\\"), std::string::npos);
}

// ===== #1493: reader rendezvous on a fifo marker =====
// WinuxCmd readers that bypassed file_io::open_binary_file read the 7
// marker bytes as file content instead of blocking on the named-pipe
// rendezvous (#1038 bridge). Each test below plants a fifo, starts a
// background writer (tee.exe, whose operand write goes through the
// write-bridge), then runs the reader under test and pins GNU byte
// output. Expected bytes mirror WSL GNU bash coreutils 9.x output for
// the same payloads.

namespace mkfifo_rendezvous_detail {

// RAII background `tee <fifo>` writer fed from a payload file. tee's
// operand open blocks in the write-bridge until the reader creates the
// pipe endpoint, exactly like `printf ... > fifo &` under GNU.
struct BgWriter {
  PROCESS_INFORMATION pi{};
  STARTUPINFOW si{};
  HANDLE hStdin{nullptr};
  HANDLE hNull{nullptr};
  bool alive{false};
  DWORD error{0};  // 0 when the writer spawned cleanly (assert before run)

  ~BgWriter() { teardown(); }

  void start(const std::wstring &cwd, const std::wstring &operand,
             const std::wstring &payload_path) {
    // STARTF_USESTDHANDLES only hands over handles marked inheritable
    // (CreateFileW defaults to non-inheritable); without the attribute
    // tee would see a broken stdin, exit without writing, and the
    // reader would block forever on the rendezvous.
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    hStdin = CreateFileW(payload_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                         &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    hNull = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &inheritable,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hStdin == INVALID_HANDLE_VALUE || hNull == INVALID_HANDLE_VALUE) {
      error = GetLastError();
      return;
    }

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdin;
    si.hStdOutput = hNull;
    si.hStdError = hNull;

    const std::filesystem::path tee = ProjectPaths::entry_binary(L"tee.exe");
    std::wstring cmdline = L"\"" + tee.wstring() + L"\" \"" + operand + L"\"";
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    alive = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si,
                           &pi) != 0;
    if (!alive) error = GetLastError();
  }

  // true when the writer exited on its own within ms.
  bool exited_within(DWORD ms) {
    return alive && WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0;
  }

  void teardown() {
    if (alive) {
      if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
      }
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      alive = false;
    }
    if (hStdin && hStdin != INVALID_HANDLE_VALUE) CloseHandle(hStdin);
    if (hNull && hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    hStdin = hNull = nullptr;
  }
};

// Plant a fifo and a payload file, return the payload's wide path.
auto plant_fifo(const TempDir &tmp, const wchar_t *fifo,
                const std::string &payload_rel,
                const std::string &payload) -> std::wstring {
  tmp.write(payload_rel, payload);
  Pipeline mk;
  mk.set_cwd(tmp.wpath());
  mk.add(L"mkfifo.exe", {fifo});
  if (mk.run().exit_code != 0) return {};
  return (tmp.path / payload_rel).wstring();
}

// A reader that never rendezvoused leaves its writer looping in the
// write-bridge forever; fail with a pointed message after the grace
// period instead of hanging the suite.
void require_writer_exited(BgWriter &w) {
  // A reader that never rendezvoused leaves the writer looping in the
  // write-bridge; the assertion fails (rather than hangs) via the
  // BgWriter destructor's terminate.
  EXPECT_TRUE(w.exited_within(10000));
}

}  // namespace mkfifo_rendezvous_detail

TEST(mkfifo, od_reads_fifo_payload_not_marker_bytes) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "hi\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"od.exe", {L"-c", L"pipe1"});
  auto r = p.run();

  TEST_LOG_CMD_LIST("od.exe", L"-c", L"pipe1");
  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  // GNU od -c output for "hi\n", byte for byte.
  EXPECT_EQ(r.stdout_text, "0000000   h   i  \\n\n0000003\n");
  require_writer_exited(w);
}

TEST(mkfifo, wc_c_counts_bridged_bytes_not_marker_size) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "hi\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"-c", L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  // GNU wc -c reads the fifo (3 bytes); the marker file's st_size of 7
  // must not be consulted (GNU stats only regular files).
  EXPECT_EQ(r.stdout_text, "3 pipe1\n");
  require_writer_exited(w);
}

TEST(mkfifo, wc_full_counts_on_fifo) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "a\nb\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"wc.exe", {L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.stdout_text, "      2       2       4 pipe1\n");
  require_writer_exited(w);
}

TEST(mkfifo, md5sum_hashes_writer_bytes) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "hi\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "764efa883dda1e11db47671c4a3bbd9e  pipe1\n");
  require_writer_exited(w);
}

TEST(mkfifo, cksum_crc_reads_writer_bytes) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "hi\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"cksum.exe", {L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  // GNU cksum of "hi\n": CRC then byte count read from the pipe.
  EXPECT_EQ(r.stdout_text, "1479881546 3 pipe1\n");
  require_writer_exited(w);
}

TEST(mkfifo, paste_joins_two_fifos) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  Pipeline mk;
  mk.set_cwd(tmp.wpath());
  mk.add(L"mkfifo.exe", {L"p", L"q"});
  ASSERT_EQ(mk.run().exit_code, 0);
  tmp.write("payload_p.txt", "1\n2\n");
  tmp.write("payload_q.txt", "x\ny\n");

  BgWriter wp;
  wp.start(tmp.wpath(), L"p", (tmp.path / "payload_p.txt").wstring());
  BgWriter wq;
  wq.start(tmp.wpath(), L"q", (tmp.path / "payload_q.txt").wstring());
  ASSERT_EQ(wp.error, 0u);
  ASSERT_EQ(wq.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"p", L"q"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  // rubash#2473 consumer shape: paste over two procsub fifos.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1\tx\n2\ty\n");
  require_writer_exited(wp);
  require_writer_exited(wq);
}

TEST(mkfifo, comm_reads_two_fifos) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  Pipeline mk;
  mk.set_cwd(tmp.wpath());
  mk.add(L"mkfifo.exe", {L"p", L"q"});
  ASSERT_EQ(mk.run().exit_code, 0);
  tmp.write("payload_p.txt", "a\nc\n");
  tmp.write("payload_q.txt", "b\nc\n");

  BgWriter wp;
  wp.start(tmp.wpath(), L"p", (tmp.path / "payload_p.txt").wstring());
  BgWriter wq;
  wq.start(tmp.wpath(), L"q", (tmp.path / "payload_q.txt").wstring());
  ASSERT_EQ(wp.error, 0u);
  ASSERT_EQ(wq.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"comm.exe", {L"p", L"q"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a\n\tb\n\t\tc\n");
  require_writer_exited(wp);
  require_writer_exited(wq);
}

TEST(mkfifo, nl_numbers_fifo_lines) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "x\ny\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "     1\tx\n     2\ty\n");
  require_writer_exited(w);
}

TEST(mkfifo, hexdump_dumps_fifo_payload) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload = plant_fifo(tmp, L"pipe1", "payload.txt", "hi\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hexdump.exe", {L"-c", L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EQ(r.exit_code, 0);
  // hexdump pads the short line; pin the payload columns and the final
  // offset rather than the full padded line.
  EXPECT_CONTAINS(r.stdout_text, "h   i  \\n");
  EXPECT_CONTAINS(r.stdout_text, "0000003");
  require_writer_exited(w);
}

TEST(mkfifo, uniq_read_all_survives_broken_pipe_eof) {
  using namespace mkfifo_rendezvous_detail;
  TempDir tmp;
  const auto payload =
      plant_fifo(tmp, L"pipe1", "payload.txt", "a\nb\nb\nc\n");
  ASSERT_FALSE(payload.empty());

  BgWriter w;
  w.start(tmp.wpath(), L"pipe1", payload);
  ASSERT_EQ(w.error, 0u);

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"uniq.exe", {L"pipe1"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  // read_all_file bridges to the raw pipe handle: when the writer
  // closes, ReadFile fails with ERROR_BROKEN_PIPE and the accumulated
  // bytes must survive as clean EOF content, not an error (#1493).
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "a\nb\nc\n");
  require_writer_exited(w);
}

TEST(mkfifo, reader_without_writer_blocks_like_gnu) {
  TempDir tmp;
  Pipeline mk;
  mk.set_cwd(tmp.wpath());
  mk.add(L"mkfifo.exe", {L"pipe1"});
  ASSERT_EQ(mk.run().exit_code, 0);

  // GNU open(O_RDONLY) on a fifo with no writer blocks; a WinuxCmd
  // reader must block the same way (not print the marker bytes) and
  // stay cancellable.
  const std::filesystem::path od = ProjectPaths::entry_binary(L"od.exe");
  std::wstring cmdline = L"\"" + od.wstring() + L"\" -c pipe1";
  std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
  buf.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const DWORD spawn_err =
      CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, tmp.wpath().c_str(), &si, &pi)
          ? 0
          : GetLastError();
  ASSERT_EQ(spawn_err, 0u);

  // Still blocked after a grace period, and produced nothing.
  const DWORD wait = WaitForSingleObject(pi.hProcess, 1000);
  // GNU semantics: reader on a writer-less fifo must block, not exit.
  EXPECT_EQ(wait, WAIT_TIMEOUT);

  TerminateProcess(pi.hProcess, 1);
  WaitForSingleObject(pi.hProcess, 3000);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
}
