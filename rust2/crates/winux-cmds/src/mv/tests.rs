//! Filesystem-level unit tests for the mv port — ScratchCtx scratch
//! roots, MemIo captured stdio. The GNU contracts asserted here were
//! probed against WSL coreutils 9.4-3ubuntu6.2 (LC_ALL=C.UTF-8).

use std::ffi::OsString;
use std::fs;

use winux_core::test_support::{MemIo, ScratchCtx};

use super::mv_cmd;

fn os(s: &str) -> OsString {
    OsString::from(s)
}

struct Run {
    rc: u32,
    out: String,
    err: String,
}

fn run(s: &ScratchCtx, args: &[&str], stdin: &str) -> Run {
    let ctx = s.ctx();
    let mut mio = MemIo::new(stdin);
    let argv: Vec<OsString> = args.iter().map(|a| os(a)).collect();
    let rc = mv_cmd(&argv, &mut mio.split(), &ctx).unwrap_or(u32::MAX);
    Run {
        rc,
        out: mio.out(),
        err: mio.err(),
    }
}

fn write(s: &ScratchCtx, rel: &str, data: &str) {
    fs::write(s.path(rel), data).unwrap();
}

fn read(s: &ScratchCtx, rel: &str) -> String {
    fs::read_to_string(s.path(rel)).unwrap()
}

#[test]
fn renames_a_file() {
    let s = ScratchCtx::new("mv-basic");
    write(&s, "a", "A");
    let r = run(&s, &["a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert!(!s.path("a").exists());
    assert_eq!(read(&s, "b"), "A");
}

#[test]
fn verbose_reports_the_rename() {
    let s = ScratchCtx::new("mv-verb");
    write(&s, "a", "A");
    let r = run(&s, &["-v", "a", "b"], "");
    assert_eq!(r.rc, 0);
    assert_eq!(r.out, "renamed 'a' -> 'b'\n");
}

#[test]
fn moves_into_a_directory() {
    let s = ScratchCtx::new("mv-intodir");
    write(&s, "a", "A");
    fs::create_dir(s.path("d")).unwrap();
    let r = run(&s, &["a", "d"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "d/a"), "A");
}

#[test]
fn missing_source_is_cannot_stat() {
    let s = ScratchCtx::new("mv-miss");
    let r = run(&s, &["nosuch", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: cannot stat 'nosuch': No such file or directory\n"
    );
}

#[test]
fn missing_operands_report_usage() {
    let s = ScratchCtx::new("mv-oper");
    let r = run(&s, &[] as &[&str], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: missing file operand\nTry 'mv --help' for more information.\n"
    );
    let r = run(&s, &["a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: missing destination file operand after 'a'\nTry 'mv --help' for more information.\n"
    );
}

#[test]
fn same_file_is_refused() {
    let s = ScratchCtx::new("mv-same");
    write(&s, "a", "A");
    let r = run(&s, &["a", "a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: 'a' and 'a' are the same file\n");
    assert_eq!(read(&s, "a"), "A");
}

#[test]
fn same_file_through_dot() {
    let s = ScratchCtx::new("mv-samedot");
    write(&s, "a", "A");
    let r = run(&s, &["a", "./a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: 'a' and './a' are the same file\n");
}

#[test]
fn hard_link_pair_is_the_same_file() {
    let s = ScratchCtx::new("mv-hard");
    write(&s, "a", "A");
    fs::hard_link(s.path("a"), s.path("b")).unwrap();
    let r = run(&s, &["a", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: 'a' and 'b' are the same file\n");
}

/// #1649: a directory onto a non-directory is refused, target intact.
#[test]
fn dir_onto_non_dir_is_refused() {
    let s = ScratchCtx::new("mv-1649");
    fs::create_dir(s.path("d")).unwrap();
    write(&s, "d/x", "X");
    write(&s, "f", "F");
    let r = run(&s, &["d", "f"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: cannot overwrite non-directory 'f' with directory 'd'\n"
    );
    assert_eq!(read(&s, "f"), "F");
    assert!(s.path("d").is_dir());
}

#[test]
fn non_dir_onto_dir_is_refused() {
    let s = ScratchCtx::new("mv-fd");
    write(&s, "a", "A");
    fs::create_dir(s.path("d")).unwrap();
    // file 'a' into dir 'd' is a normal move; -T forces the refusal.
    let r = run(&s, &["-T", "a", "d"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: cannot overwrite directory 'd' with non-directory\n"
    );
}

#[test]
fn into_itself_is_refused() {
    let s = ScratchCtx::new("mv-self");
    fs::create_dir_all(s.path("d/sub")).unwrap();
    // GNU 9.4: `d/sub` already is a directory, so the last operand is the
    // target directory and the destination is `d/sub/d` (probed: `mv d
    // d/sub` -> "cannot move 'd' to a subdirectory of itself, 'd/sub/d'").
    let r = run(&s, &["d", "d/sub"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: cannot move 'd' to a subdirectory of itself, 'd/sub/d'\n"
    );
    assert!(s.path("d/sub").is_dir());
}

#[test]
fn force_replaces_the_destination() {
    let s = ScratchCtx::new("mv-force");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-f", "a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "b"), "A");
    assert!(!s.path("a").exists());
}

#[test]
fn no_clobber_skips_and_reports() {
    let s = ScratchCtx::new("mv-n");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-n", "a", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: not replacing 'b'\n");
    assert_eq!(read(&s, "b"), "B");
    assert_eq!(read(&s, "a"), "A");
}

#[test]
fn interactive_decline_and_accept() {
    let s = ScratchCtx::new("mv-i");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-i", "a", "b"], "n\n");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: overwrite 'b'? ");
    assert_eq!(read(&s, "b"), "B");

    write(&s, "c", "C");
    let r = run(&s, &["-i", "c", "b"], "y\n");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(r.err, "mv: overwrite 'b'? ");
    assert_eq!(read(&s, "b"), "C");
}

#[test]
fn last_wins_interactive_family() {
    let s = ScratchCtx::new("mv-lastwins");
    // -i then -f: force wins, no prompt.
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-i", "-f", "a", "b"], "");
    assert_eq!(r.rc, 0);
    assert_eq!(r.err, "");
    // -f then -n: no-clobber wins.
    write(&s, "c", "C");
    let r = run(&s, &["-f", "-n", "c", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: not replacing 'b'\n");
}

#[test]
fn update_only_moves_when_source_is_newer() {
    let s = ScratchCtx::new("mv-u");
    write(&s, "a", "A");
    write(&s, "b", "B");
    // 'b' newer than 'a': skipped silently, rc 0.
    let r = run(&s, &["-u", "a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "a"), "A");
    // 'c' newer than 'b': moved.
    std::thread::sleep(std::time::Duration::from_millis(20));
    write(&s, "c", "C");
    let r = run(&s, &["-u", "c", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "b"), "C");
    assert!(!s.path("c").exists());
}

#[test]
fn backup_renames_the_destination() {
    let s = ScratchCtx::new("mv-bak");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-b", "a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "b"), "A");
    assert_eq!(read(&s, "b~"), "B");
}

#[test]
fn backup_verbose_mentions_the_backup() {
    let s = ScratchCtx::new("mv-bakv");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-b", "-v", "a", "b"], "");
    assert_eq!(r.rc, 0);
    assert_eq!(r.out, "renamed 'a' -> 'b' (backup: 'b~')\n");
}

#[test]
fn backup_suffix_is_honored() {
    let s = ScratchCtx::new("mv-suf");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["-b", "-S", ".bak", "a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "b.bak"), "B");
}

#[test]
fn backing_up_might_destroy_source() {
    let s = ScratchCtx::new("mv-baksrc");
    write(&s, "a~", "TILDE");
    write(&s, "a", "A");
    let r = run(&s, &["-b", "a~", "a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: backing up 'a' might destroy source;  'a~' not moved\n"
    );
}

#[test]
fn target_directory_moves_every_operand() {
    let s = ScratchCtx::new("mv-t");
    write(&s, "a", "A");
    write(&s, "b", "B");
    fs::create_dir(s.path("d")).unwrap();
    let r = run(&s, &["-t", "d", "a", "b"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "d/a"), "A");
    assert_eq!(read(&s, "d/b"), "B");
}

#[test]
fn target_directory_must_be_a_directory() {
    let s = ScratchCtx::new("mv-tf");
    write(&s, "a", "A");
    write(&s, "f", "F");
    let r = run(&s, &["-t", "f", "a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: target directory 'f': Not a directory\n");
    let r = run(&s, &["-t", "nodir", "a"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: target directory 'nodir': No such file or directory\n"
    );
}

#[test]
fn non_dir_multi_source_target_reports() {
    let s = ScratchCtx::new("mv-multi");
    write(&s, "a", "A");
    write(&s, "b", "B");
    let r = run(&s, &["a", "b", "notdir"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: target 'notdir': No such file or directory\n");
}

#[test]
fn strip_trailing_slashes_renames_dir_operand() {
    let s = ScratchCtx::new("mv-strip");
    fs::create_dir(s.path("d")).unwrap();
    write(&s, "d/x", "X");
    let r = run(&s, &["--strip-trailing-slashes", "d/", "e"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert!(s.path("e").is_dir());
    assert!(!s.path("d").exists());
}

#[test]
fn trailing_slash_on_file_is_enotdir() {
    let s = ScratchCtx::new("mv-slash");
    write(&s, "a", "A");
    let r = run(&s, &["a/", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: cannot stat 'a/': Not a directory\n");
}

#[test]
fn just_created_destination_is_not_overwritten() {
    let s = ScratchCtx::new("mv-justcreated");
    fs::create_dir_all(s.path("a")).unwrap();
    fs::create_dir_all(s.path("b")).unwrap();
    fs::create_dir_all(s.path("d")).unwrap();
    write(&s, "a/f", "F1");
    write(&s, "b/f", "F2");
    let r = run(&s, &["-b", "a/f", "b/f", "d"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: will not overwrite just-created 'd/f' with 'b/f'\n"
    );
    assert_eq!(read(&s, "d/f"), "F1");
}

#[test]
fn case_rename_of_a_file_works() {
    let s = ScratchCtx::new("mv-case");
    write(&s, "File.txt", "DATA");
    let r = run(&s, &["File.txt", "file.txt"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "file.txt"), "DATA");
}

#[test]
fn moves_a_directory_with_contents() {
    let s = ScratchCtx::new("mv-dir");
    fs::create_dir_all(s.path("d/sub")).unwrap();
    write(&s, "d/a", "A");
    write(&s, "d/sub/b", "B");
    let r = run(&s, &["d", "e"], "");
    assert_eq!(r.rc, 0, "stderr: {}", r.err);
    assert_eq!(read(&s, "e/a"), "A");
    assert_eq!(read(&s, "e/sub/b"), "B");
}

#[test]
fn option_errors_carry_gnu_wording() {
    let s = ScratchCtx::new("mv-opt");
    let r = run(&s, &["--bogus", "a", "b"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: unrecognized option '--bogus'\nTry 'mv --help' for more information.\n"
    );
    let r = run(&s, &["-S"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(
        r.err,
        "mv: option requires an argument -- 'S'\nTry 'mv --help' for more information.\n"
    );
    let r = run(&s, &["-t", "a", "-t", "b", "c"], "");
    assert_eq!(r.rc, 1);
    assert_eq!(r.err, "mv: multiple target directories specified\n");
}
