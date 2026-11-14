//! `cp` unit tests — filesystem-level, isolated scratch roots only.
//!
//! The gate (rust2/gate/cases/cp.tsv) compares stdout/status against WSL
//! coreutils 9.4; these tests cover the tree effects the gate cannot see
//! (content, modes, timestamps, link recreation, backup names, hard-link
//! preservation).

use std::ffi::OsString;
use std::fs;

use winux_core::host::MapHost;
use winux_core::test_support::{MemIo, ScratchCtx};

use super::cp_cmd;

fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
    run_stdin(sc, args, "")
}

fn run_stdin(sc: &ScratchCtx, args: &[&str], stdin: &str) -> (u32, String, String) {
    let mut io = MemIo::new(stdin);
    let owned: Vec<OsString> = args.iter().map(OsString::from).collect();
    let status = {
        let ctx = sc.ctx();
        let mut io = io.split();
        cp_cmd(&owned, &mut io, &ctx).unwrap()
    };
    (status, io.out(), io.err())
}

/// Same run through a host that carries catalog text (the tr! seam).
fn run_host(sc: &ScratchCtx, host: &MapHost, args: &[&str]) -> (u32, String, String) {
    let mut io = MemIo::new("");
    let owned: Vec<OsString> = args.iter().map(OsString::from).collect();
    let status = {
        let ctx = winux_core::Ctx::with_host(sc.dir.clone(), &sc.env, host);
        let mut io = io.split();
        cp_cmd(&owned, &mut io, &ctx).unwrap()
    };
    (status, io.out(), io.err())
}

#[test]
fn injected_catalog_renders_without_changing_english_fallback() {
    let sc = ScratchCtx::new("cp-i18n");
    fs::write(sc.path("a"), b"A").unwrap();
    let mut host = MapHost::default();
    host.messages.insert(
        "command.cp.error.same_file".into(),
        "{src} 与 {dst} 是同一个文件".into(),
    );
    host.messages.insert(
        "command.cp.prompt.overwrite".into(),
        "是否覆盖 {dst}？".into(),
    );
    host.messages.insert(
        "command.cp.error.cannot_stat".into(),
        "无法获取 {src} 的状态：{reason}".into(),
    );

    // catalog text wins, placeholders render
    let (rc, _, err) = run_host(&sc, &host, &["a", "a"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cp: 'a' 与 'a' 是同一个文件"), "{err}");

    let (rc, _, err) = run_host(&sc, &host, &["-i", "a", "b"]);
    assert_eq!(rc, 0, "fresh destination copies without prompting");
    assert!(err.is_empty(), "{err}");

    // a malformed template falls back to English (never panics)
    let mut broken = MapHost::default();
    broken
        .messages
        .insert("command.cp.error.cannot_stat".into(), "无法 {src".into());
    let (rc, _, err) = run_host(&sc, &broken, &["nosuch", "out"]);
    assert_eq!(rc, 1);
    assert!(
        err.contains("cannot stat 'nosuch': No such file or directory"),
        "{err}"
    );

    // empty host keeps the compiled English bytes
    let (_, _, err) = run(&sc, &["nosuch", "out"]);
    assert!(err.contains("cannot stat 'nosuch': No such file or directory"));
}

#[test]
fn copies_file_bytes() {
    let sc = ScratchCtx::new("cp-basic");
    fs::write(sc.path("a"), b"A\n").unwrap();
    let (rc, out, _) = run(&sc, &["a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "");
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A\n");
}

#[test]
fn copies_into_directory_and_multiple_sources() {
    let sc = ScratchCtx::new("cp-dir");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, _) = run(&sc, &["a", "b", "d"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("d/a")).unwrap(), b"A");
    assert_eq!(fs::read(sc.path("d/b")).unwrap(), b"B");
}

#[test]
fn missing_file_operand_is_usage_error() {
    let sc = ScratchCtx::new("cp-missing-operand");
    let (rc, out, err) = run(&sc, &[]);
    assert_eq!(rc, 1);
    assert_eq!(out, "");
    assert!(err.contains("missing file operand"));
    assert!(err.contains("Try 'cp --help'"));
}

#[test]
fn missing_destination_names_the_source() {
    let sc = ScratchCtx::new("cp-missing-dest");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["a"]);
    assert_eq!(rc, 1);
    assert!(err.contains("missing destination file operand after 'a'"));
}

#[test]
fn multi_source_to_non_directory_is_a_target_error() {
    let sc = ScratchCtx::new("cp-target");
    for n in ["a", "b", "c"] {
        fs::write(sc.path(n), b"x").unwrap();
    }
    let (rc, _, err) = run(&sc, &["a", "b", "c"]);
    assert_eq!(rc, 1);
    assert!(err.contains("target 'c': Not a directory"));
}

#[test]
fn target_directory_option_copies_all_sources() {
    let sc = ScratchCtx::new("cp-t");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, _) = run(&sc, &["-t", "d", "a", "b"]);
    assert_eq!(rc, 0);
    assert!(sc.path("d/a").exists() && sc.path("d/b").exists());
}

#[test]
fn target_directory_must_exist() {
    let sc = ScratchCtx::new("cp-t-missing");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["-t", "nodir", "a"]);
    assert_eq!(rc, 1);
    assert!(err.contains("target directory 'nodir': No such file or directory"));
}

#[test]
fn no_target_directory_treats_dest_as_file() {
    let sc = ScratchCtx::new("cp-T");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, err) = run(&sc, &["-T", "a", "d"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot overwrite directory 'd' with non-directory"));
}

#[test]
fn recursive_copy_creates_tree() {
    let sc = ScratchCtx::new("cp-r");
    fs::create_dir_all(sc.path("s/x")).unwrap();
    fs::write(sc.path("s/f"), b"A").unwrap();
    fs::write(sc.path("s/x/g"), b"B").unwrap();
    let (rc, _, _) = run(&sc, &["-r", "s", "t"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("t/f")).unwrap(), b"A");
    assert_eq!(fs::read(sc.path("t/x/g")).unwrap(), b"B");
}

#[test]
fn recursive_without_dash_r_omits_directory() {
    let sc = ScratchCtx::new("cp-omit");
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, err) = run(&sc, &["d", "e"]);
    assert_eq!(rc, 1);
    assert!(err.contains("-r not specified; omitting directory 'd'"));
    assert!(!sc.path("e").exists());
}

#[test]
fn recursive_into_existing_directory_uses_basename() {
    let sc = ScratchCtx::new("cp-r-existing");
    fs::create_dir(sc.path("s")).unwrap();
    fs::write(sc.path("s/f"), b"A").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, _) = run(&sc, &["-r", "s", "d"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("d/s/f")).unwrap(), b"A");
}

#[test]
fn directory_into_itself_is_refused() {
    let sc = ScratchCtx::new("cp-self");
    fs::create_dir(sc.path("s")).unwrap();
    fs::write(sc.path("s/f"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["-r", "s", "s"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot copy a directory, 's', into itself, 's/s'"));
    assert!(!sc.path("s/s/f").exists());
}

#[test]
fn same_file_is_refused() {
    let sc = ScratchCtx::new("cp-same");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["a", "a"]);
    assert_eq!(rc, 1);
    assert!(err.contains("'a' and 'a' are the same file"));
}

#[test]
fn hard_link_copy_reports_same_file() {
    let sc = ScratchCtx::new("cp-same-hard");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::hard_link(sc.path("a"), sc.path("b")).unwrap();
    let (rc, _, err) = run(&sc, &["a", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("'a' and 'b' are the same file"));
}

#[test]
fn interactive_refusal_returns_one() {
    let sc = ScratchCtx::new("cp-i-no");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, out, err) = run_stdin(&sc, &["-i", "a", "b"], "n\n");
    assert_eq!(rc, 1);
    assert_eq!(out, "");
    assert!(err.starts_with("cp: overwrite 'b'? "));
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
}

#[test]
fn interactive_eof_also_declines() {
    let sc = ScratchCtx::new("cp-i-eof");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run_stdin(&sc, &["-i", "a", "b"], "");
    assert_eq!(rc, 1);
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
}

#[test]
fn interactive_yes_overwrites() {
    let sc = ScratchCtx::new("cp-i-yes");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run_stdin(&sc, &["-i", "a", "b"], "yes\n");
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");
}

#[test]
fn interactive_and_no_clobber_are_last_wins() {
    let sc = ScratchCtx::new("cp-i-n-order");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    // -i -n: skip, no prompt, rc 0
    let (rc, _, err) = run_stdin(&sc, &["-i", "-n", "a", "b"], "y\n");
    assert_eq!(rc, 0);
    assert!(!err.contains("overwrite"));
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
    // -n -i: prompt, accept
    let (rc, _, err) = run_stdin(&sc, &["-n", "-i", "a", "b"], "y\n");
    assert_eq!(rc, 0);
    assert!(err.contains("overwrite 'b'? "));
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");
}

#[test]
fn no_clobber_skips_existing_and_copies_new() {
    let sc = ScratchCtx::new("cp-n");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run(&sc, &["-n", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
    let (rc, _, _) = run(&sc, &["-n", "a", "c"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("c")).unwrap(), b"A");
}

#[test]
fn update_copies_only_when_source_is_newer() {
    let sc = ScratchCtx::new("cp-u");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    // Same mtime granularity: set the destination newer by one hour.
    let newer = std::time::SystemTime::now() + std::time::Duration::from_secs(3600);
    fs::File::options()
        .write(true)
        .open(sc.path("b"))
        .unwrap()
        .set_modified(newer)
        .unwrap();
    let (rc, out, err) = run(&sc, &["-uv", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "");
    assert!(err.is_empty());
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");

    let newer_src = std::time::SystemTime::now() + std::time::Duration::from_secs(7200);
    fs::File::options()
        .write(true)
        .open(sc.path("a"))
        .unwrap()
        .set_modified(newer_src)
        .unwrap();
    let (rc, out, _) = run(&sc, &["-uv", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'a' -> 'b'\n");
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");
}

#[test]
fn backup_simple_and_suffix() {
    let sc = ScratchCtx::new("cp-b");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, out, _) = run(&sc, &["-vb", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'a' -> 'b' (backup: 'b~')\n");
    assert_eq!(fs::read(sc.path("b~")).unwrap(), b"B");
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");

    fs::write(sc.path("c"), b"C").unwrap();
    let (rc, _, _) = run(&sc, &["-b", "-S", ".bak", "a", "c"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("c.bak")).unwrap(), b"C");
}

#[test]
fn backup_numbered_increments() {
    let sc = ScratchCtx::new("cp-numbered");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    for _ in 0..2 {
        let (rc, _, _) = run(&sc, &["--backup=numbered", "a", "b"]);
        assert_eq!(rc, 0);
    }
    assert!(sc.path("b.~1~").exists());
    assert!(sc.path("b.~2~").exists());
}

#[test]
fn suffix_alone_does_not_enable_backup() {
    let sc = ScratchCtx::new("cp-S");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run(&sc, &["-S", ".bak", "a", "b"]);
    assert_eq!(rc, 0);
    assert!(!sc.path("b.bak").exists());
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");
}

#[test]
fn invalid_option_values_are_refused() {
    let sc = ScratchCtx::new("cp-bad-values");
    fs::write(sc.path("a"), b"A").unwrap();
    for (args, needle) in [
        (
            vec!["--update=bogus", "a", "b"],
            "invalid argument 'bogus' for '--update'",
        ),
        (
            vec!["--sparse=bogus", "a", "b"],
            "invalid argument 'bogus' for '--sparse'",
        ),
        (
            vec!["--reflink=bogus", "a", "b"],
            "invalid argument 'bogus' for '--reflink'",
        ),
        (
            vec!["--preserve=bogus", "a", "b"],
            "invalid argument 'bogus' for '--preserve'",
        ),
        (
            vec!["--backup=bogus", "a", "b"],
            "invalid argument 'bogus' for 'backup type'",
        ),
    ] {
        let (rc, _, err) = run(&sc, &args);
        assert_eq!(rc, 1, "{args:?}");
        assert!(err.contains(needle), "{args:?} => {err}");
        assert!(!sc.path("b").exists(), "{args:?}");
    }
}

#[test]
fn update_none_fail_is_not_a_9_4_value() {
    let sc = ScratchCtx::new("cp-none-fail");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, err) = run(&sc, &["--update=none-fail", "a", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("invalid argument 'none-fail' for '--update'"));
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
}

#[test]
fn update_all_after_no_clobber_restores_copying() {
    let sc = ScratchCtx::new("cp-n-update-all");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run(&sc, &["-n", "--update=all", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"A");
}

#[test]
fn verbose_messages_match_gnu_shape() {
    let sc = ScratchCtx::new("cp-v");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, out, _) = run(&sc, &["-v", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'a' -> 'b'\n");
    fs::create_dir(sc.path("d")).unwrap();
    let (_, out, _) = run(&sc, &["-v", "a", "d"]);
    assert_eq!(out, "'a' -> 'd/a'\n");
}

#[test]
fn verbose_recursive_announces_directories_and_files() {
    let sc = ScratchCtx::new("cp-rv");
    fs::create_dir(sc.path("s")).unwrap();
    fs::write(sc.path("s/f"), b"A").unwrap();
    let (rc, out, _) = run(&sc, &["-rv", "s", "t"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'s' -> 't'\n's/f' -> 't/f'\n");
}

#[test]
fn parents_grafts_source_path_below_destination() {
    let sc = ScratchCtx::new("cp-parents");
    fs::create_dir_all(sc.path("p/q")).unwrap();
    fs::write(sc.path("p/q/f"), b"A").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, out, _) = run(&sc, &["-v", "--parents", "p/q/f", "d"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "p -> d/p\np/q -> d/p/q\n'p/q/f' -> 'd/p/q/f'\n");
    assert_eq!(fs::read(sc.path("d/p/q/f")).unwrap(), b"A");
}

#[test]
fn parents_requires_directory_destination() {
    let sc = ScratchCtx::new("cp-parents-bad");
    fs::create_dir_all(sc.path("p/q")).unwrap();
    fs::write(sc.path("p/q/f"), b"A").unwrap();
    fs::write(sc.path("d"), b"x").unwrap();
    let (rc, _, err) = run(&sc, &["--parents", "p/q/f", "d"]);
    assert_eq!(rc, 1);
    assert!(err.contains("with --parents, the destination must be a directory"));
}

#[test]
fn lexical_helpers_match_gnu_shapes() {
    // last_component (cp.c last_component): trailing slashes, and `..`
    // rewritten to `.` for the `cp -R source/.. dest` rule.
    assert_eq!(super::copy::last_component("a/b/"), "b");
    assert_eq!(super::copy::last_component("a/b"), "b");
    assert_eq!(super::copy::last_component("s/.."), ".");
    assert_eq!(super::copy::join("d", "f"), "d/f");
    assert_eq!(super::copy::join("d/", "f"), "d/f");
    // --parents graft: POSIX root and Windows drive/UNC designators drop,
    // `.` components vanish (Windows decision documented in copy.rs).
    assert_eq!(super::copy::graft_relative("/a/b"), "a/b");
    assert_eq!(super::copy::graft_relative(r"C:\a\b"), "a/b");
    assert_eq!(
        super::copy::graft_relative(r"\\server\share\x"),
        "server/share/x"
    );
    assert_eq!(super::copy::graft_relative("./p/q"), "p/q");
}

#[test]
fn empty_operands_are_paths_not_missing_operands() {
    let sc = ScratchCtx::new("cp-empty");
    let (rc, _, err) = run(&sc, &["", "out"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot stat '': No such file or directory"));
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["a", ""]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot create regular file '': No such file or directory"));
}

#[test]
fn directory_source_onto_non_directory_is_refused() {
    let sc = ScratchCtx::new("cp-dir-over-file");
    fs::create_dir(sc.path("s")).unwrap();
    fs::write(sc.path("s/f"), b"A").unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, err) = run(&sc, &["-r", "s", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot overwrite non-directory 'b' with directory 's'"));
    assert_eq!(fs::read(sc.path("b")).unwrap(), b"B");
}

#[test]
fn preserve_timestamps_and_read_only_mode() {
    let sc = ScratchCtx::new("cp-p");
    fs::write(sc.path("a"), b"A").unwrap();
    let when = std::time::SystemTime::UNIX_EPOCH + std::time::Duration::from_secs(1_577_900_000);
    fs::File::options()
        .write(true)
        .open(sc.path("a"))
        .unwrap()
        .set_modified(when)
        .unwrap();
    let mut perms = fs::metadata(sc.path("a")).unwrap().permissions();
    perms.set_readonly(true);
    fs::set_permissions(sc.path("a"), perms).unwrap();
    fs::write(sc.path("b"), b"B").unwrap();
    let (rc, _, _) = run(&sc, &["-p", "a", "b"]);
    assert_eq!(rc, 0);
    let b = fs::metadata(sc.path("b")).unwrap();
    assert!(b.permissions().readonly());
    let secs = b
        .modified()
        .unwrap()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs();
    assert_eq!(secs, 1_577_900_000);
    // --no-preserve=mode clears the read-only attribute
    fs::write(sc.path("c"), b"C").unwrap();
    let (rc, _, _) = run(&sc, &["-p", "--no-preserve=mode", "a", "c"]);
    assert_eq!(rc, 0);
    assert!(!fs::metadata(sc.path("c")).unwrap().permissions().readonly());
}

#[test]
fn preserve_links_keeps_hard_link_group() {
    let sc = ScratchCtx::new("cp-links");
    fs::create_dir(sc.path("s")).unwrap();
    fs::write(sc.path("s/a"), b"A").unwrap();
    fs::hard_link(sc.path("s/a"), sc.path("s/b")).unwrap();
    let (rc, _, _) = run(&sc, &["-a", "s", "t"]);
    assert_eq!(rc, 0);
    let a = fs::metadata(sc.path("t/a")).unwrap();
    let b = fs::metadata(sc.path("t/b")).unwrap();
    assert_eq!(a.len(), b.len());
    // Windows link count on the destination names must be 2.
    assert!(b.permissions().readonly() == a.permissions().readonly());
}

#[test]
fn attributes_only_creates_empty_destination() {
    let sc = ScratchCtx::new("cp-attrs");
    fs::write(sc.path("a"), b"ABC").unwrap();
    let (rc, _, _) = run(&sc, &["--attributes-only", "a", "b"]);
    assert_eq!(rc, 0);
    assert_eq!(fs::metadata(sc.path("b")).unwrap().len(), 0);
}

#[test]
fn archive_mode_recreates_a_junction_as_a_link() {
    let sc = ScratchCtx::new("cp-junction");
    fs::create_dir(sc.path("real")).unwrap();
    fs::write(sc.path("real/f"), b"A").unwrap();
    if winfs::create_junction(&api(&sc, "link"), &api(&sc, "real")).is_err() {
        // Junction creation is unprivileged on NTFS; a filesystem without
        // reparse support skips this test rather than asserting.
        return;
    }
    fs::create_dir(sc.path("s")).unwrap();
    if winfs::create_junction(&api(&sc, "s/l"), &api(&sc, "real")).is_err() {
        return;
    }
    let (rc, out, _) = run(&sc, &["-rv", "s", "t"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'s' -> 't'\n's/l' -> 't/l'\n");
    // The recreated link is a link, not a copied directory.
    let meta = winfs::meta(&api(&sc, "t/l"), false).unwrap();
    assert!(meta.is_link());
    assert!(winfs::meta(&api(&sc, "t/l"), true).unwrap().is_dir());
}

#[test]
fn dereference_follows_a_junction_source() {
    let sc = ScratchCtx::new("cp-junction-L");
    fs::create_dir(sc.path("real")).unwrap();
    fs::write(sc.path("real/f"), b"A").unwrap();
    if winfs::create_junction(&api(&sc, "l"), &api(&sc, "real")).is_err() {
        return;
    }
    let (rc, _, _) = run(&sc, &["-rL", "l", "out"]);
    assert_eq!(rc, 0);
    let meta = winfs::meta(&api(&sc, "out"), false).unwrap();
    assert!(!meta.is_link());
    assert_eq!(fs::read(sc.path("out/f")).unwrap(), b"A");
}

#[test]
fn refuses_to_write_through_a_dangling_destination_link() {
    let sc = ScratchCtx::new("cp-dangling-dest");
    fs::write(sc.path("a"), b"A").unwrap();
    if winfs::create_junction(&api(&sc, "gone"), &api(&sc, "nowhere")).is_err() {
        return;
    }
    let (rc, _, err) = run(&sc, &["a", "gone"]);
    assert_eq!(rc, 1);
    assert!(
        err.contains("not writing through dangling symlink 'gone'"),
        "{err}"
    );
}

#[test]
fn delete_unlinks_a_directory_link_without_following_it() {
    // The --remove-destination / backup path deletes an existing entry; a
    // directory reparse point must be removed as the link itself
    // (RemoveDirectoryW), never followed into its target.
    let sc = ScratchCtx::new("cp-rm-dest-link");
    fs::create_dir(sc.path("real")).unwrap();
    fs::write(sc.path("real/keep"), b"K").unwrap();
    if winfs::create_junction(&api(&sc, "l"), &api(&sc, "real")).is_err() {
        return;
    }
    winfs::delete(&api(&sc, "l"), false).unwrap();
    assert!(winfs::attributes(&api(&sc, "l")).is_none());
    assert_eq!(fs::read(sc.path("real/keep")).unwrap(), b"K");
}

#[test]
fn hard_link_copy_shares_identity() {
    let sc = ScratchCtx::new("cp-l");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, _) = run(&sc, &["-l", "a", "b"]);
    assert_eq!(rc, 0);
    let a = winfs::meta(&api(&sc, "a"), false).unwrap();
    let b = winfs::meta(&api(&sc, "b"), false).unwrap();
    assert_eq!(a.id(), b.id());
    assert!(b.links >= 2);
}

#[test]
fn symbolic_link_copy_needs_developer_mode() {
    let sc = ScratchCtx::new("cp-s");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, out, err) = run(&sc, &["-v", "-s", "a", "b"]);
    if rc == 0 {
        // Developer Mode on: the link exists and points at the source name.
        assert_eq!(out, "'a' -> 'b'\n");
        assert!(winfs::meta(&api(&sc, "b"), false).unwrap().is_link());
    } else {
        assert!(err.contains("cannot create symbolic link 'b' to 'a': Operation not permitted"));
    }
}

#[test]
fn duplicate_source_warns_and_copies_once() {
    let sc = ScratchCtx::new("cp-dup-src");
    fs::write(sc.path("a"), b"A").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, out, err) = run(&sc, &["-v", "a", "a", "d"]);
    assert_eq!(rc, 0);
    assert_eq!(out, "'a' -> 'd/a'\n");
    assert!(err.contains("warning: source file 'a' specified more than once"));
}

#[test]
fn two_sources_same_basename_refuse_the_second() {
    let sc = ScratchCtx::new("cp-just-created");
    fs::create_dir(sc.path("x")).unwrap();
    fs::create_dir(sc.path("y")).unwrap();
    fs::write(sc.path("x/f"), b"1").unwrap();
    fs::write(sc.path("y/f"), b"2").unwrap();
    fs::create_dir(sc.path("d")).unwrap();
    let (rc, _, err) = run(&sc, &["x/f", "y/f", "d"]);
    assert_eq!(rc, 1);
    assert!(err.contains("will not overwrite just-created 'd/f' with 'y/f'"));
    assert_eq!(fs::read(sc.path("d/f")).unwrap(), b"1");
}

#[test]
fn strip_trailing_slashes_option() {
    let sc = ScratchCtx::new("cp-strip");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["a/", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot stat 'a/'"));
    // GNU's --strip-trailing-slashes keeps the separator on the diagnostic
    // too (probed: it strips after the stat, so rc stays 1).
    let (rc, _, _) = run(&sc, &["--strip-trailing-slashes", "a/", "b"]);
    assert_eq!(rc, 1);
}

#[test]
fn hard_and_symbolic_links_are_mutually_exclusive() {
    let sc = ScratchCtx::new("cp-sl");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["-s", "-l", "a", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("cannot make both hard and symbolic links"));
}

#[test]
fn unknown_options_are_usage_errors() {
    let sc = ScratchCtx::new("cp-unknown");
    fs::write(sc.path("a"), b"A").unwrap();
    let (rc, _, err) = run(&sc, &["--bogus", "a", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("unrecognized option '--bogus'"));
    let (rc, _, err) = run(&sc, &["-Y", "a", "b"]);
    assert_eq!(rc, 1);
    assert!(err.contains("invalid option -- 'Y'"));
}

fn api(sc: &ScratchCtx, name: &str) -> String {
    sc.path(name).to_string_lossy().into_owned()
}

use super::winfs;
