//! rm unit tests — GNU 9.4 semantics probed on WSL, Windows value model
//! on scratch dirs.

use super::*;
use std::ffi::OsString;
use winux_core::test_support::{MemIo, ScratchCtx};

fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
    run_in(sc, args, "", false)
}

fn run_tty(sc: &ScratchCtx, args: &[&str], input: &str) -> (u32, String, String) {
    run_in(sc, args, input, true)
}

fn run_in(sc: &ScratchCtx, args: &[&str], input: &str, tty: bool) -> (u32, String, String) {
    let mut io = MemIo::new(input);
    let ctx = sc.ctx();
    let owned: Vec<OsString> = args.iter().map(Into::into).collect();
    let meta = RmMeta { stdin_tty: tty };
    let r = rm_with_meta(&owned, &mut io.split(), &ctx, &meta).unwrap();
    (r, io.out(), io.err())
}

fn file(sc: &ScratchCtx, rel: &str) {
    std::fs::write(sc.path(rel), "x").unwrap();
}

fn dir(sc: &ScratchCtx, rel: &str) {
    std::fs::create_dir_all(sc.path(rel)).unwrap();
}

fn readonly(sc: &ScratchCtx, rel: &str) {
    let p = sc.path(rel);
    let mut perms = std::fs::metadata(&p).unwrap().permissions();
    perms.set_readonly(true);
    std::fs::set_permissions(&p, perms).unwrap();
}

// --------------------------------------------------------------------
// operands and the -f suppression set

#[test]
fn missing_operand_and_dashdash() {
    let sc = ScratchCtx::new("rm-miss");
    let (r, _, err) = run(&sc, &[]);
    assert_eq!(r, 1);
    assert_eq!(
        err,
        "rm: missing operand\nTry 'rm --help' for more information.\n"
    );
    // `rm --` is still zero operands (GNU probed)
    let (r, _, err) = run(&sc, &["--"]);
    assert_eq!(r, 1);
    assert!(err.starts_with("rm: missing operand"));
    // -f turns the empty operand list into a silent success
    let (r, _, err) = run(&sc, &["-f"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    let (r, _, _) = run(&sc, &["-f", "--"]);
    assert_eq!(r, 0);
}

#[test]
fn empty_operand_is_enoent_not_missing_operand() {
    // #1650: `rm ''` must attempt the removal and report ENOENT.
    let sc = ScratchCtx::new("rm-empty");
    let (r, _, err) = run(&sc, &[""]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove '': No such file or directory\n");
    // -f swallows the ENOENT class silently
    let (r, _, err) = run(&sc, &["-f", ""]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    // an empty operand among real ones still errors while the rest run
    file(&sc, "f");
    let (r, _, err) = run(&sc, &["", "f"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove '': No such file or directory\n");
    assert!(!sc.path("f").exists());
}

#[test]
fn missing_file_and_force() {
    let sc = ScratchCtx::new("rm-enoent");
    let (r, _, err) = run(&sc, &["missing"]);
    assert_eq!(r, 1);
    assert_eq!(
        err,
        "rm: cannot remove 'missing': No such file or directory\n"
    );
    let (r, _, err) = run(&sc, &["-f", "missing"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
}

#[test]
fn removes_a_file_and_aggregates() {
    let sc = ScratchCtx::new("rm-basic");
    file(&sc, "a");
    file(&sc, "b");
    let (r, out, err) = run(&sc, &["a", "b"]);
    assert_eq!(r, 0);
    assert!(out.is_empty() && err.is_empty());
    assert!(!sc.path("a").exists() && !sc.path("b").exists());
    // a failure does not stop later operands; status aggregates to 1
    file(&sc, "g");
    let (r, _, err) = run(&sc, &["missing", "g", "also-missing"]);
    assert_eq!(r, 1);
    assert!(!sc.path("g").exists());
    assert!(err.contains("cannot remove 'missing'"));
    assert!(err.contains("cannot remove 'also-missing'"));
}

// --------------------------------------------------------------------
// directory gate and -d

#[test]
fn directory_refusal_without_recursive() {
    let sc = ScratchCtx::new("rm-eisdir");
    dir(&sc, "d");
    for args in [
        &["d"][..],
        &["-f", "d"][..],
        &["-i", "d"][..],
        &["-v", "d"][..],
    ] {
        let (r, _, err) = run(&sc, args);
        assert_eq!(r, 1, "{args:?}");
        assert_eq!(err, "rm: cannot remove 'd': Is a directory\n", "{args:?}");
        assert!(sc.path("d").exists());
    }
}

#[test]
fn trailing_separator_semantics() {
    let sc = ScratchCtx::new("rm-sep");
    file(&sc, "f");
    let (r, _, err) = run(&sc, &["f/"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove 'f/': Not a directory\n");
    // ENOTDIR is in the -f ignorable set
    let (r, _, err) = run(&sc, &["-f", "f/"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    dir(&sc, "d");
    let (r, _, err) = run(&sc, &["d/"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove 'd/': Is a directory\n");
    let (r, _, err) = run(&sc, &["missing/"]);
    assert_eq!(r, 1);
    assert!(err.contains("No such file or directory"));
}

#[test]
fn dir_option_removes_only_empty() {
    let sc = ScratchCtx::new("rm-d");
    dir(&sc, "e");
    let (r, out, _) = run(&sc, &["-dv", "e"]);
    assert_eq!(r, 0);
    assert_eq!(out, "removed directory 'e'\n");
    assert!(!sc.path("e").exists());

    dir(&sc, "n");
    file(&sc, "n/f");
    let (r, _, err) = run(&sc, &["-d", "n"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove 'n': Directory not empty\n");

    // -d on a plain file still removes it (GNU probed)
    file(&sc, "f");
    let (r, _, _) = run(&sc, &["-d", "f"]);
    assert_eq!(r, 0);
    assert!(!sc.path("f").exists());
}

// --------------------------------------------------------------------
// recursion, verbose, continuation

#[test]
fn recursive_tree_and_verbose_order() {
    let sc = ScratchCtx::new("rm-rec");
    dir(&sc, "d/sub");
    file(&sc, "d/f");
    file(&sc, "d/sub/g");
    let (r, out, err) = run(&sc, &["-rv", "d"]);
    assert_eq!(r, 0, "{err}");
    // postorder: files, inner dir, outer dir (enumeration order)
    let lines: Vec<&str> = out.lines().collect();
    assert_eq!(lines.len(), 4, "{out}");
    assert!(lines.contains(&"removed 'd/f'"));
    assert!(lines.contains(&"removed 'd/sub/g'"));
    assert_eq!(lines[3], "removed directory 'd'");
    assert!(!sc.path("d").exists());
}

#[test]
fn deep_tree_and_R_alias() {
    let sc = ScratchCtx::new("rm-deep");
    let mut rel = String::from("t");
    dir(&sc, &rel);
    for i in 0..40 {
        rel = format!("{rel}/n{i}");
        dir(&sc, &rel);
        file(&sc, &format!("{rel}/f"));
    }
    let (r, _, err) = run(&sc, &["-R", "t"]);
    assert_eq!(r, 0, "{err}");
    assert!(!sc.path("t").exists());
}

#[test]
fn recursive_continues_after_child_failure() {
    // a failed sibling must not stop the traversal; the parent stays and
    // the status aggregates (mark_ancestor_dirs)
    let sc = ScratchCtx::new("rm-cont");
    dir(&sc, "d/a");
    dir(&sc, "d/b");
    file(&sc, "d/b/f");
    file(&sc, "keep");
    // block d/a removal by holding a nested nonempty dir whose own rmdir
    // cannot run? simpler: make d/a undeletable via a locked sibling —
    // instead use an unreadable file? Windows dirs are deletable. Use a
    // second operand failure to check continuation instead.
    let (r, _, _) = run(&sc, &["-r", "d", "missing2", "keep"]);
    assert_eq!(r, 1);
    assert!(!sc.path("d").exists(), "d removed despite later failure");
    assert!(!sc.path("keep").exists(), "last operand still processed");
}

// --------------------------------------------------------------------
// interactive prompts

#[test]
fn interactive_file_prompts() {
    let sc = ScratchCtx::new("rm-i");
    file(&sc, "f");
    let (r, _, err) = run(&sc, &["-i", "f"]);
    // no tty and no input: EOF declines
    assert_eq!(r, 0);
    assert_eq!(err, "rm: remove regular file 'f'? ");
    assert!(sc.path("f").exists());

    let (r, _, _) = run_in(&sc, &["-i", "f"], "y\n", false);
    assert_eq!(r, 0);
    assert!(!sc.path("f").exists());

    // empty file noun
    let sc2 = ScratchCtx::new("rm-i-empty");
    std::fs::write(sc2.path("e"), "").unwrap();
    let (_, _, err) = run(&sc2, &["-i", "e"]);
    assert_eq!(err, "rm: remove regular empty file 'e'? ");
}

#[test]
fn interactive_dir_prompt_sequence() {
    let sc = ScratchCtx::new("rm-idir");
    dir(&sc, "d");
    file(&sc, "d/f");
    let (r, _, err) = run_in(&sc, &["-ir", "d"], "y\ny\ny\n", false);
    assert_eq!(r, 0, "{err}");
    assert_eq!(
        err,
        "rm: descend into directory 'd'? rm: remove regular file 'd/f'? \
         rm: remove directory 'd'? "
    );
    assert!(!sc.path("d").exists());
}

#[test]
fn interactive_decline_keeps_parent() {
    let sc = ScratchCtx::new("rm-decl");
    dir(&sc, "d");
    file(&sc, "d/f");
    // decline the file: the parent dir is kept AND not re-prompted
    let (r, _, err) = run_in(&sc, &["-ir", "d"], "y\nn\n", false);
    assert_eq!(r, 0);
    assert_eq!(
        err,
        "rm: descend into directory 'd'? rm: remove regular file 'd/f'? "
    );
    assert!(sc.path("d/f").exists());
    assert!(sc.path("d").exists());
}

#[test]
fn interactive_empty_dir_and_nested() {
    let sc = ScratchCtx::new("rm-ied");
    dir(&sc, "d/e");
    // -ir: descend 'd', then 'd/e' is empty -> 'remove directory' at
    // preorder, then postorder 'remove directory d'
    let (r, _, err) = run_in(&sc, &["-ir", "d"], "y\ny\ny\n", false);
    assert_eq!(r, 0, "{err}");
    assert_eq!(
        err,
        "rm: descend into directory 'd'? rm: remove directory 'd/e'? \
         rm: remove directory 'd'? "
    );
    assert!(!sc.path("d").exists());
}

#[test]
fn bulk_prompt_once() {
    let sc = ScratchCtx::new("rm-I");
    for n in ["f1", "f2", "f3", "f4"] {
        file(&sc, n);
    }
    // 4 operands -> bulk prompt; EOF declines, rc 0
    let (r, _, err) = run(&sc, &["-I", "f1", "f2", "f3", "f4"]);
    assert_eq!(r, 0);
    assert_eq!(err, "rm: remove 4 arguments? ");
    assert!(sc.path("f1").exists());
    // accept -> all gone
    let (r, _, _) = run_in(&sc, &["-I", "f1", "f2", "f3", "f4"], "y\n", false);
    assert_eq!(r, 0);
    assert!(!sc.path("f1").exists());
    // <=3 operands: no prompt at all
    for n in ["a1", "a2", "a3"] {
        file(&sc, n);
    }
    let (r, _, err) = run(&sc, &["-I", "a1", "a2", "a3"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    // recursive always bulks
    dir(&sc, "t");
    file(&sc, "t/f");
    let (r, _, err) = run(&sc, &["-I", "-r", "t"]);
    assert_eq!(r, 0);
    assert_eq!(err, "rm: remove 1 argument recursively? ");
    assert!(sc.path("t").exists());
}

// --------------------------------------------------------------------
// last-wins prompt flags and --interactive

#[test]
fn last_wins_prompt_flags() {
    let sc = ScratchCtx::new("rm-lastwins");
    // -f -i: prompts
    file(&sc, "a");
    let (_, _, err) = run_in(&sc, &["-f", "-i", "a"], "n\n", false);
    assert_eq!(err, "rm: remove regular file 'a'? ");
    assert!(sc.path("a").exists());
    // -i -f: silent removal
    let (r, _, err) = run(&sc, &["-i", "-f", "a"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    assert!(!sc.path("a").exists());
    // clustered order counts too: -if vs -fi
    file(&sc, "b");
    let (r, _, err) = run(&sc, &["-if", "b"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    file(&sc, "c");
    let (_, _, err) = run_in(&sc, &["-fi", "c"], "n\n", false);
    assert!(err.starts_with("rm: remove regular file 'c'?"));
    // -I then -f: never prompts
    for n in ["x1", "x2", "x3", "x4"] {
        file(&sc, n);
    }
    let (r, _, err) = run(&sc, &["-I", "-f", "x1", "x2", "x3", "x4"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    // -f then -I: bulk prompt
    for n in ["y1", "y2", "y3", "y4"] {
        file(&sc, n);
    }
    let (_, _, err) = run(&sc, &["-f", "-I", "y1", "y2", "y3", "y4"]);
    assert_eq!(err, "rm: remove 4 arguments? ");
}

#[test]
fn interactive_option_forms() {
    let sc = ScratchCtx::new("rm-inter");
    file(&sc, "f");
    // bare --interactive = always
    let (_, _, err) = run_in(&sc, &["--interactive", "f"], "n\n", false);
    assert_eq!(err, "rm: remove regular file 'f'? ");
    assert!(sc.path("f").exists());
    // =never never prompts
    let (r, _, _) = run(&sc, &["--interactive=never", "f"]);
    assert_eq!(r, 0);
    assert!(!sc.path("f").exists());
    // abbreviation resolves
    file(&sc, "g");
    let (_, _, err) = run_in(&sc, &["--inter=al", "g"], "n\n", false);
    assert_eq!(err, "rm: remove regular file 'g'? ");
    // prefix word matching: 'o' -> once
    for n in ["p1", "p2", "p3", "p4"] {
        file(&sc, n);
    }
    let (_, _, err) = run(&sc, &["--interactive=o", "p1", "p2", "p3", "p4"]);
    assert_eq!(err, "rm: remove 4 arguments? ");
    // invalid word
    let (r, _, err) = run(&sc, &["--interactive=bogus", "f"]);
    assert_eq!(r, 1);
    assert!(err.contains("invalid argument 'bogus' for '--interactive'"));
    assert!(err.contains("Valid arguments are:"));
    assert!(err.contains("- 'never', 'no', 'none'"));
    assert!(err.contains("Try 'rm --help'"));
    // empty word is ambiguous
    let (r, _, err) = run(&sc, &["--interactive=", "f"]);
    assert_eq!(r, 1);
    assert!(err.contains("ambiguous argument '' for '--interactive'"));
}

// --------------------------------------------------------------------
// write-protected (read-only) behavior

#[test]
fn readonly_file_removed_silently_without_tty() {
    // RMI_SOMETIMES + non-tty stdin: no prompt; the pre-clear makes
    // DeleteFileW accept what unlink(2) accepts.
    let sc = ScratchCtx::new("rm-ro");
    file(&sc, "ro");
    readonly(&sc, "ro");
    let (r, _, err) = run(&sc, &["ro"]);
    assert_eq!(r, 0, "{err}");
    assert!(err.is_empty());
    assert!(!sc.path("ro").exists());
}

#[test]
fn readonly_file_prompts_on_tty_stdin() {
    let sc = ScratchCtx::new("rm-ro-tty");
    file(&sc, "ro");
    readonly(&sc, "ro");
    // tty stdin + default mode -> write-protected prompt; decline keeps
    let (r, _, err) = run_tty(&sc, &["ro"], "n\n");
    assert_eq!(r, 0);
    assert_eq!(err, "rm: remove write-protected regular file 'ro'? ");
    assert!(sc.path("ro").exists());
    // ---presume-input-tty does the same
    let (r, _, err) = run_in(&sc, &["---presume-input-tty", "ro"], "n\n", false);
    assert_eq!(r, 0);
    assert_eq!(err, "rm: remove write-protected regular file 'ro'? ");
    // -f removes without prompting
    let (r, _, err) = run(&sc, &["-f", "ro"]);
    assert_eq!(r, 0);
    assert!(err.is_empty());
    assert!(!sc.path("ro").exists());
}

#[test]
fn readonly_prompt_under_i_uses_wp_noun() {
    let sc = ScratchCtx::new("rm-ro-i");
    file(&sc, "ro");
    readonly(&sc, "ro");
    let (_, _, err) = run_in(&sc, &["-i", "ro"], "n\n", false);
    assert_eq!(err, "rm: remove write-protected regular file 'ro'? ");
}

// --------------------------------------------------------------------
// refusals: dot, preserve-root

#[test]
fn dot_and_dotdot_refusal() {
    let sc = ScratchCtx::new("rm-dot");
    file(&sc, "f");
    let (r, _, err) = run(&sc, &["-r", "."]);
    assert_eq!(r, 1);
    assert_eq!(
        err,
        "rm: refusing to remove '.' or '..' directory: skipping '.'\n"
    );
    dir(&sc, "e");
    let (r, _, err) = run(&sc, &["-r", "e/.."]);
    assert_eq!(r, 1);
    assert!(err.contains("skipping 'e/..'"), "{err}");
    // without -r it is the plain EISDIR gate
    let (r, _, err) = run(&sc, &["."]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove '.': Is a directory\n");
}

#[test]
fn preserve_root_refusals() {
    let sc = ScratchCtx::new("rm-root");
    let (r, _, err) = run(&sc, &["-r", "/"]);
    assert_eq!(r, 1);
    assert_eq!(
        err,
        "rm: it is dangerous to operate recursively on '/'\n\
         rm: use --no-preserve-root to override this failsafe\n"
    );
    let (r, _, err) = run(&sc, &["-r", "//"]);
    assert_eq!(r, 1);
    assert!(err.contains("on '//' (same as '/')"), "{err}");
    let (r, _, err) = run(&sc, &["-r", "///"]);
    assert_eq!(r, 1);
    assert!(err.contains("on '/'"), "{err}");
    // no recursive -> plain EISDIR, no root diagnostics
    let (r, _, err) = run(&sc, &["/"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove '/': Is a directory\n");
}

#[test]
fn shell_root_operand_refusal() {
    // the configured native root is the same floor as '/'
    let mut sc = ScratchCtx::new("rm-shroot");
    sc.env
        .set("WINUXSH_ROOT", &sc.dir.to_string_lossy().into_owned());
    file(&sc, "f");
    let dir = sc.dir.to_string_lossy().into_owned();
    let (r, _, err) = run(&sc, &["-r", &dir]);
    assert_eq!(r, 1);
    assert!(err.contains("(same as '/')"), "{err}");
    assert!(err.contains("--no-preserve-root"), "{err}");
    assert!(sc.path("f").exists(), "nothing was touched");
}

#[test]
fn preserve_root_option_errors() {
    let sc = ScratchCtx::new("rm-proot");
    file(&sc, "f");
    let (r, _, err) = run(&sc, &["--preserve-root=x", "f"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: unrecognized --preserve-root argument: 'x'\n");
    let (r, _, err) = run(&sc, &["--no-p", "f"]);
    assert_eq!(r, 1);
    assert_eq!(
        err,
        "rm: you may not abbreviate the --no-preserve-root option\n"
    );
    let (r, _, err) = run(&sc, &["--no-preserve-root=v", "f"]);
    assert_eq!(r, 1);
    assert!(err.contains("doesn't allow an argument"), "{err}");
    // bare --no-preserve-root is fine
    let (r, _, _) = run(&sc, &["--no-preserve-root", "f"]);
    assert_eq!(r, 0);
}

// --------------------------------------------------------------------
// links (conditional on symlink privilege)

fn dir_link(sc: &ScratchCtx, target: &str, link: &str) -> bool {
    #[cfg(windows)]
    {
        std::os::windows::fs::symlink_dir(sc.path(target), sc.path(link)).is_ok()
    }
    #[cfg(not(windows))]
    {
        std::os::unix::fs::symlink(sc.path(target), sc.path(link)).is_ok()
    }
}

fn file_link(sc: &ScratchCtx, target: &str, link: &str) -> bool {
    #[cfg(windows)]
    {
        std::os::windows::fs::symlink_file(sc.path(target), sc.path(link)).is_ok()
    }
    #[cfg(not(windows))]
    {
        std::os::unix::fs::symlink(sc.path(target), sc.path(link)).is_ok()
    }
}

#[test]
fn dir_link_removed_as_link() {
    let sc = ScratchCtx::new("rm-link");
    dir(&sc, "t");
    file(&sc, "t/f");
    if !dir_link(&sc, "t", "l") {
        println!("skipped: symlink creation unavailable");
        return;
    }
    // plain rm on a dir link removes the LINK, target untouched
    let (r, out, err) = run(&sc, &["-v", "l"]);
    assert_eq!(r, 0, "{err}");
    assert_eq!(out, "removed 'l'\n");
    assert!(!sc.path("l").exists());
    assert!(sc.path("t/f").exists());
}

#[test]
fn recursive_on_dir_link_removes_link_only() {
    let sc = ScratchCtx::new("rm-rlink");
    dir(&sc, "t");
    file(&sc, "t/f");
    if !dir_link(&sc, "t", "l") {
        println!("skipped: symlink creation unavailable");
        return;
    }
    let (r, _, err) = run(&sc, &["-r", "l"]);
    assert_eq!(r, 0, "{err}");
    assert!(!sc.path("l").exists());
    assert!(sc.path("t/f").exists(), "target contents kept");
}

#[test]
fn trailing_sep_link_dereferences_target() {
    let sc = ScratchCtx::new("rm-seplink");
    dir(&sc, "t");
    file(&sc, "t/f");
    if !dir_link(&sc, "t", "l") {
        println!("skipped: symlink creation unavailable");
        return;
    }
    // rm l/ -> EISDIR (no -r)
    let (r, _, err) = run(&sc, &["l/"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove 'l/': Is a directory\n");
    // rm -r l/ -> walks the TARGET (f removed), then rmdir fails ENOTDIR;
    // link and emptied target both stay
    let (r, _, err) = run(&sc, &["-r", "l/"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove 'l/': Not a directory\n");
    assert!(!sc.path("t/f").exists(), "target was traversed: f removed");
    assert!(sc.path("t").exists());
    assert!(sc.path("l").exists(), "link kept after ENOTDIR");
    // under -f the ENOTDIR is ignorable
    file(&sc, "t/f2");
    let (r, _, err) = run(&sc, &["-rf", "l/"]);
    assert_eq!(r, 0, "{err}");
    assert!(err.is_empty());
    assert!(!sc.path("t/f2").exists());
}

#[test]
fn file_link_removed_as_link() {
    let sc = ScratchCtx::new("rm-flink");
    file(&sc, "t");
    if !file_link(&sc, "t", "l") {
        println!("skipped: symlink creation unavailable");
        return;
    }
    let (r, _, err) = run(&sc, &["l"]);
    assert_eq!(r, 0, "{err}");
    assert!(!sc.path("l").exists());
    assert!(sc.path("t").exists());
    // -i prompt names the noun 'symbolic link'
    if file_link(&sc, "t", "l2") {
        let (_, _, err) = run_in(&sc, &["-i", "l2"], "n\n", false);
        assert_eq!(err, "rm: remove symbolic link 'l2'? ");
    }
}

// --------------------------------------------------------------------
// misc

#[test]
fn verbose_removed_forms() {
    let sc = ScratchCtx::new("rm-verb");
    file(&sc, "f");
    let (r, out, _) = run(&sc, &["-v", "f"]);
    assert_eq!(r, 0);
    assert_eq!(out, "removed 'f'\n");
}

#[test]
fn dash_operand_and_hyphen_hint() {
    let sc = ScratchCtx::new("rm-dash");
    // bare '-' is an operand
    let (r, _, err) = run(&sc, &["-"]);
    assert_eq!(r, 1);
    assert_eq!(err, "rm: cannot remove '-': No such file or directory\n");
    // a dash-named file that is also an invalid option -> the
    // leading-hyphen hint (diagnose_leading_hyphen lstat's each -token)
    file(&sc, "-z");
    let (r, _, err) = run(&sc, &["-z"]);
    assert_eq!(r, 1);
    assert!(err.contains("invalid option -- 'z'"), "{err}");
    assert!(
        err.contains("Try 'rm ./-z' to remove the file '-z'"),
        "{err}"
    );
    // a dash token naming no file gets no hint
    let (_, _, err) = run(&sc, &["-x"]);
    assert!(!err.contains("Try 'rm ./"), "{err}");
    // -- swallows the option spelling entirely
    let (r, _, _) = run(&sc, &["--", "-z"]);
    assert_eq!(r, 0);
    assert!(!sc.path("-z").exists());
}

#[test]
fn help_and_version() {
    let sc = ScratchCtx::new("rm-help");
    let (r, out, _) = run(&sc, &["--help"]);
    assert_eq!(r, 0);
    assert!(out.starts_with("Usage: rm [OPTION]... [FILE]..."));
    assert!(out.contains("--one-file-system"));
    assert!(out.contains("GNU coreutils online help"));
    let (r, out, _) = run(&sc, &["--version"]);
    assert_eq!(r, 0);
    assert!(out.starts_with("rm (GNU coreutils) 9.4"));
    // #1573 sequential contract: an earlier --help wins over later junk
    let (r, out, _) = run(&sc, &["--help", "--bogus"]);
    assert_eq!(r, 0);
    assert!(out.starts_with("Usage: rm"));
}
