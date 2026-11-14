//! Unit tests for `find` (MemIo + ScratchCtx fixtures). Output order of a
//! directory is the volume's enumeration order, so multi-entry results are
//! compared sorted.

use winux_core::test_support::{os, MemIo, ScratchCtx};

use super::find_cmd;

fn fixture(tag: &str) -> ScratchCtx {
    let sc = ScratchCtx::new(&format!("find-{tag}"));
    std::fs::create_dir_all(sc.path("t/a/b")).unwrap();
    std::fs::create_dir_all(sc.path("t/d")).unwrap();
    std::fs::write(sc.path("t/a/f5"), b"hello").unwrap();
    std::fs::write(sc.path("t/a/b/big"), vec![0u8; 3000]).unwrap();
    for f in ["t/e", "t/d/.hid", "t/a/x.rs", "t/a/b/y.rs"] {
        std::fs::write(sc.path(f), b"").unwrap();
    }
    sc
}

fn run(sc: &ScratchCtx, args: &[&str], stdin: &str) -> (u32, String, String) {
    let mut m = MemIo::new(stdin);
    let ctx = sc.ctx();
    let rc = {
        let mut io = m.split();
        find_cmd(&os(args), &mut io, &ctx).unwrap()
    };
    (rc, m.out(), m.err())
}

fn sorted(s: &str) -> Vec<String> {
    let mut v: Vec<String> = s.lines().map(|l| l.to_string()).collect();
    v.sort();
    v
}

#[test]
fn default_walk_lists_everything() {
    let sc = fixture("walk");
    let (rc, out, _) = run(&sc, &["t"], "");
    assert_eq!(rc, 0);
    assert_eq!(
        sorted(&out),
        [
            "t",
            "t/a",
            "t/a/b",
            "t/a/b/big",
            "t/a/b/y.rs",
            "t/a/f5",
            "t/a/x.rs",
            "t/d",
            "t/d/.hid",
            "t/e"
        ]
    );
}

#[test]
fn preorder_and_depth_order() {
    let sc = ScratchCtx::new("find-order");
    std::fs::create_dir_all(sc.path("c/d/e")).unwrap();
    assert_eq!(run(&sc, &["c"], "").1, "c\nc/d\nc/d/e\n");
    assert_eq!(run(&sc, &["c", "-depth"], "").1, "c/d/e\nc/d\nc\n");
}

#[test]
fn root_spellings() {
    let sc = fixture("roots");
    let (_, out, _) = run(&sc, &["t/", "-maxdepth", "1"], "");
    assert_eq!(sorted(&out), ["t/", "t/a", "t/d", "t/e"]);
    let (_, out, _) = run(&sc, &["./t", "-maxdepth", "0"], "");
    assert_eq!(out, "./t\n");
    let (rc, out, err) = run(&sc, &["@"; 0], "");
    assert_eq!(rc, 0);
    assert!(out.starts_with(".\n"), "{out}{err}");
    let (rc, _, err) = run(&sc, &[""], "");
    assert_eq!(rc, 1);
    assert!(err.contains("No such file or directory"));
    let (rc, _, err) = run(&sc, &["t/e/"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("Not a directory"), "{err}");
    let (rc, out, _) = run(&sc, &["t/d", "nosuch", "t/e"], "");
    assert_eq!(rc, 1);
    assert_eq!(sorted(&out), ["t/d", "t/d/.hid", "t/e"]);
}

#[test]
fn depth_limits() {
    let sc = fixture("depth");
    assert_eq!(run(&sc, &["t", "-maxdepth", "0"], "").1, "t\n");
    let (_, out, _) = run(&sc, &["t", "-mindepth", "2", "-maxdepth", "2"], "");
    assert_eq!(sorted(&out), ["t/a/b", "t/a/f5", "t/a/x.rs", "t/d/.hid"]);
}

#[test]
fn name_path_regex() {
    let sc = fixture("names");
    assert_eq!(
        sorted(&run(&sc, &["t", "-name", "*.rs"], "").1),
        ["t/a/b/y.rs", "t/a/x.rs"]
    );
    assert_eq!(run(&sc, &["t", "-iname", "F5"], "").1, "t/a/f5\n");
    assert_eq!(run(&sc, &["t/", "-name", "t"], "").1, "t/\n");
    assert_eq!(
        sorted(&run(&sc, &["t", "-path", "t/a/*.rs"], "").1),
        ["t/a/b/y.rs", "t/a/x.rs"]
    );
    assert_eq!(
        sorted(&run(&sc, &["t", "-regex", ".*\\.rs"], "").1),
        ["t/a/b/y.rs", "t/a/x.rs"]
    );
    assert_eq!(
        run(&sc, &["t", "-regex", "t/a/f\\(5\\|6\\)"], "").1,
        "t/a/f5\n"
    );
    let (rc, _, err) = run(&sc, &["t", "-regex", "\\(a"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("Unmatched ( or \\("), "{err}");
}

#[test]
fn types_sizes_empty() {
    let sc = fixture("types");
    assert_eq!(
        sorted(&run(&sc, &["t", "-type", "d"], "").1),
        ["t", "t/a", "t/a/b", "t/d"]
    );
    assert_eq!(
        run(&sc, &["t", "-type", "f", "-size", "5c"], "").1,
        "t/a/f5\n"
    );
    assert_eq!(
        run(&sc, &["t", "-type", "f", "-size", "6"], "").1,
        "t/a/b/big\n"
    );
    assert_eq!(
        run(&sc, &["t", "-type", "f", "-size", "3k"], "").1,
        "t/a/b/big\n"
    );
    assert_eq!(
        sorted(&run(&sc, &["t", "-empty"], "").1),
        ["t/a/b/y.rs", "t/a/x.rs", "t/d/.hid", "t/e"]
    );
    let (rc, _, err) = run(&sc, &["t", "-type", "f,f"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("Duplicate file type 'f'"));
}

#[test]
fn operators_and_precedence() {
    let sc = fixture("ops");
    assert_eq!(
        run(&sc, &["t", "-name", "f5", "-o", "-name", "e", "-print"], "").1,
        "t/e\n"
    );
    assert_eq!(
        sorted(&run(&sc, &["t", "(", "-name", "f5", "-o", "-name", "e", ")"], "").1),
        ["t/a/f5", "t/e"]
    );
    assert_eq!(run(&sc, &["t", "-maxdepth", "0", "-o", "-print"], "").1, "");
    assert_eq!(
        run(&sc, &["t/e", "-print", ",", "-print"], "").1,
        "t/e\nt/e\n"
    );
    for (args, want) in [
        (vec!["t", "("], "expected to find a ')'"),
        (vec!["t", "(", ")"], "empty parentheses"),
        (vec!["t", "-name", "x", ")"], "too many ')'"),
        (vec!["t", "!"], "expected an expression after '!'"),
        (
            vec!["t", "-a"],
            "binary operator '-a' with nothing before it",
        ),
        (vec!["t", "-name", "x", "-o", ")"], "between '-o' and ')'"),
        (vec!["t", "-foo"], "unknown predicate `-foo'"),
        (vec!["t", "-name"], "missing argument to `-name'"),
        (
            vec!["t", "-name", "x", "t/a"],
            "paths must precede expression",
        ),
    ] {
        let (rc, out, err) = run(&sc, &args, "");
        assert_eq!(rc, 1, "{args:?}");
        assert_eq!(out, "");
        assert!(err.contains(want), "{args:?}: {err}");
    }
}

#[test]
fn prune_quit_delete() {
    let sc = fixture("prune");
    assert_eq!(
        sorted(&run(&sc, &["t", "-name", "a", "-prune", "-o", "-print"], "").1),
        ["t", "t/d", "t/d/.hid", "t/e"]
    );
    assert_eq!(run(&sc, &["t", "-quit", "-print"], "").1, "");
    assert_eq!(run(&sc, &["t", "-print", "-quit"], "").1, "t\n");
    let (rc, out, _) = run(&sc, &["t/a", "-delete", "-print"], "");
    assert_eq!(rc, 0);
    assert_eq!(out.lines().last(), Some("t/a"));
    assert!(!sc.path("t/a").exists());
    let (rc, _, err) = run(&sc, &["t", "-maxdepth", "0", "-delete"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("cannot delete 't'"), "{err}");
    let (rc, _, err) = run(&sc, &["t", "-prune", "-delete"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("-delete action automatically turns on -depth"));
}

#[test]
fn delete_clears_readonly() {
    let sc = fixture("delro");
    let p = sc.path("t/e");
    let mut perm = std::fs::metadata(&p).unwrap().permissions();
    perm.set_readonly(true);
    std::fs::set_permissions(&p, perm).unwrap();
    assert_eq!(
        run(&sc, &["t/e", "-perm", "444", "-delete", "-print"], "").1,
        "t/e\n"
    );
    assert!(!p.exists());
}

#[test]
fn printf_directives() {
    let sc = fixture("printf");
    let (_, out, _) = run(
        &sc,
        &["t/a/f5", "-printf", "%p|%f|%h|%P|%H|%d|%y|%s|%m|%M\\n"],
        "",
    );
    assert_eq!(out, "t/a/f5|f5|t/a||t/a/f5|0|f|5|644|-rw-r--r--\n");
    let (_, out, _) = run(
        &sc,
        &[
            "t",
            "-maxdepth",
            "1",
            "-name",
            "e",
            "-printf",
            "%P|%H|%d\\n",
        ],
        "",
    );
    assert_eq!(out, "e|t|1\n");
    let (_, out, _) = run(
        &sc,
        &["t/a/f5", "-printf", "[%10p][%-4f|][%.3p][%#m][%+3d]\\n"],
        "",
    );
    assert_eq!(out, "[    t/a/f5][f5  |][t/a][0644][ +0]\n");
    let (_, out, _) = run(&sc, &["t/a/f5", "-printf", "a\\0b\\ctail"], "");
    assert_eq!(out, "a\0b");
}

#[test]
fn printf_times_fixed_tz() {
    let mut sc = fixture("times");
    sc.env.set("TZ", "UTC0");
    let f = std::fs::File::options()
        .write(true)
        .open(sc.path("t/a/f5"))
        .unwrap();
    let t = std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_577_836_800);
    f.set_times(std::fs::FileTimes::new().set_modified(t).set_accessed(t))
        .unwrap();
    drop(f);
    let (_, out, _) = run(
        &sc,
        &["t/a/f5", "-printf", "%t|%T@|%TT|%T+|%TY-%Tm-%Td|%Tc\\n"],
        "",
    );
    assert_eq!(
        out,
        "Wed Jan  1 00:00:00.0000000000 2020|1577836800.0000000000|00:00:00.0000000000|2020-01-01+00:00:00.0000000000|2020-01-01|Wed Jan  1 00:00:00 2020\n"
    );
    assert_eq!(
        run(&sc, &["t", "-type", "f", "-mtime", "+1"], "").1,
        "t/a/f5\n"
    );
    assert_eq!(
        run(&sc, &["t", "-type", "f", "!", "-newermt", "2021-01-01"], "").1,
        "t/a/f5\n"
    );
    assert_eq!(
        sorted(&run(&sc, &["t", "-type", "f", "-newer", "t/a/f5"], "").1),
        ["t/a/b/big", "t/a/b/y.rs", "t/a/x.rs", "t/d/.hid", "t/e"]
    );
}

#[test]
fn perm_model() {
    let sc = fixture("perm");
    assert_eq!(
        sorted(&run(&sc, &["t", "-perm", "755"], "").1),
        ["t", "t/a", "t/a/b", "t/d"]
    );
    assert_eq!(run(&sc, &["t", "-type", "f", "-perm", "/111"], "").1, "");
    std::fs::write(sc.path("t/run.exe"), b"").unwrap();
    assert_eq!(
        run(&sc, &["t", "-type", "f", "-perm", "-u+x"], "").1,
        "t/run.exe\n"
    );
    let (rc, _, err) = run(&sc, &["t", "-perm", "999"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("invalid mode"));
}

#[test]
fn help_version_debug() {
    let sc = ScratchCtx::new("find-help");
    let (rc, out, _) = run(&sc, &["--help"], "");
    assert_eq!(rc, 0);
    assert!(out.starts_with("Usage: find [-H] [-L] [-P]"));
    assert!(run(&sc, &["-version"], "")
        .1
        .starts_with("find (GNU findutils) 4.9.0\n"));
    assert!(run(&sc, &["-D", "help"], "")
        .1
        .starts_with("Valid arguments for -D:"));
    assert_eq!(run(&sc, &["-D"], "").0, 1);
    assert_eq!(run(&sc, &["-O"], "").0, 1);
}

#[test]
fn files0_from_and_fprint() {
    let sc = fixture("files0");
    std::fs::write(sc.path("list"), b"t/a\0t/e\0").unwrap();
    assert_eq!(
        sorted(&run(&sc, &["-files0-from", "list", "-maxdepth", "0"], "").1),
        ["t/a", "t/e"]
    );
    let (rc, out, _) = run(&sc, &["t", "-name", "f5", "-fprint", "o.txt"], "");
    assert_eq!((rc, out.as_str()), (0, ""));
    assert_eq!(
        std::fs::read_to_string(sc.path("o.txt")).unwrap(),
        "t/a/f5\n"
    );
    let (rc, _, err) = run(&sc, &[".", "-files0-from", "list"], "");
    assert_eq!(rc, 1);
    assert!(err.contains("file operands cannot be combined"));
}

#[test]
fn ok_reads_reply_from_stdin() {
    let sc = fixture("ok");
    let (rc, _, err) = run(&sc, &["t/e", "-ok", "cmd", "/c", "exit", "0", ";"], "n\n");
    assert_eq!(rc, 0);
    assert!(err.contains("< cmd ... t/e > ? "), "{err}");
}

#[test]
fn exec_status_and_batches() {
    let sc = fixture("exec");
    // `cmd /c exit 3` fails: -exec ; is a false test, -exec + sets rc 1
    let (rc, out, _) = run(
        &sc,
        &[
            "t/e", "-exec", "cmd", "/c", "exit", "3", ";", "-o", "-print",
        ],
        "",
    );
    assert_eq!((rc, out.as_str()), (0, "t/e\n"));
    let (rc, _, _) = run(
        &sc,
        &["t/e", "-exec", "cmd", "/c", "exit", "3", "{}", "+"],
        "",
    );
    assert_eq!(rc, 1);
    // GNU: an unrunnable `;` command is a false test, rc stays 0
    let (rc, _, err) = run(&sc, &["t/e", "-exec", "nosuchcmd-find-test", ";"], "");
    assert_eq!(rc, 0);
    assert!(err.contains("No such file or directory"), "{err}");
}

#[test]
fn junction_is_a_link() {
    let sc = fixture("junction");
    let ok = std::process::Command::new("cmd")
        .args(["/c", "mklink", "/J", "t\\a\\jd", "t\\d"])
        .current_dir(&sc.dir)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false);
    if !ok {
        return; // no junction support on this volume
    }
    assert_eq!(run(&sc, &["t", "-type", "l"], "").1, "t/a/jd\n");
    assert_eq!(
        run(&sc, &["t", "-xtype", "d", "-name", "jd"], "").1,
        "t/a/jd\n"
    );
    assert_eq!(
        sorted(&run(&sc, &["-L", "t/a/jd"], "").1),
        ["t/a/jd", "t/a/jd/.hid"]
    );
    assert_eq!(
        run(&sc, &["t/a/jd", "-printf", "%y%Y %m\\n"], "").1,
        "ld 777\n"
    );
    // loop: a junction back to an ancestor
    let ok = std::process::Command::new("cmd")
        .args(["/c", "mklink", "/J", "t\\d\\up", "t"])
        .current_dir(&sc.dir)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false);
    if ok {
        let (rc, _, err) = run(&sc, &["-L", "t", "-name", "nothing"], "");
        assert_eq!(rc, 1);
        assert!(err.contains("File system loop detected"), "{err}");
    }
}
