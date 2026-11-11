//! Shared GNU-shaped `--help` / `--version` text surface for the rust2
//! commands (#1488 help-surface wave).
//!
//! The differential gate (`rust2/gate/run-gate.sh`) compares the driver's
//! stdout byte-for-byte against the WSL coreutils 9.4 oracle for `strict`
//! rows, so the help/version text here is the probed oracle text, not the
//! WinuxCmd-branded product text: the product exe keeps the dispatcher's
//! sole `--help`/`--version` interception (REPLACEMENT-RECIPE.md), which
//! only ever shows the branded form. These helpers serve the standalone
//! driver and the in-process FFI path (`winuxcmd-driver --abi ...` shares
//! the same `dispatch`).
//!
//! Provenance: every body embedded next to a `gnuhelp::` call was captured
//! from `env <cmd> --help|--version` under WSL coreutils 9.4 (Ubuntu
//! 24.04, 2026-11-11) and stored under the gate probe artifacts
//! (`target/help-probe/gnu/`); the strict gate rows keep the bytes honest.

/// The GNU coreutils `--version` block: `<name> (GNU coreutils) 9.4`,
/// the GPLv3 copyright stanza and the per-command authors line.
pub fn coreutils_version(prog: &str, authors: &str) -> String {
    format!(
        "{prog} (GNU coreutils) 9.4\n\
         Copyright (C) 2023 Free Software Foundation, Inc.\n\
         License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n\
         This is free software: you are free to change and redistribute it.\n\
         There is NO WARRANTY, to the extent permitted by law.\n\
         \n\
         Written by {authors}.\n"
    )
}

/// The GNU coreutils `--help` tail block. `info_node` is the texinfo
/// node name after `info '(coreutils) '` — usually `"<prog> invocation"`,
/// except the sha2 family which shares `sha2 utilities`.
pub fn footer(prog: &str, info_node: &str) -> String {
    format!(
        "GNU coreutils online help: <https://www.gnu.org/software/coreutils/>\n\
         Report any translation bugs to <https://translationproject.org/team/>\n\
         Full documentation <https://www.gnu.org/software/coreutils/{prog}>\n\
         or available locally via: info '(coreutils) {info_node}'\n"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_block_shape() {
        let v = coreutils_version("whoami", "Richard Mlynarik");
        assert!(v.starts_with("whoami (GNU coreutils) 9.4\n"));
        assert!(v.contains("Copyright (C) 2023 Free Software Foundation, Inc.\n"));
        assert!(v.ends_with("Written by Richard Mlynarik.\n"));
    }

    #[test]
    fn footer_block_shape() {
        let f = footer("tac", "tac invocation");
        assert!(f.starts_with("GNU coreutils online help:"));
        assert!(f.contains("Full documentation <https://www.gnu.org/software/coreutils/tac>\n"));
        assert!(f.ends_with("info '(coreutils) tac invocation'\n"));
    }
}
