# WPM first-party audit — src/commands/wpm.cpp vs docs/en/wpm_guide.md

Date: 2026-09-21. Companion to the round-2 audits. wpm has no upstream;
its authority is the in-repo spec (docs/en/wpm_guide.md), the i18n help-sync
contract (scripts/sync_wpm_help.py → MANUAL_MESSAGES
"command.wpm.custom_help"), and tests/unit/wpm/wpm_unit_test.cpp.

## Headline

The binary works, but the spec, the binary, and the catalog have drifted
apart in both directions: one documented feature is broken in code
(`wpm list <query>`), the i18n help entry advertises a help line the binary
does not have, and roughly a third of user-visible strings bypass i18n
entirely (~43 raw "wpm: ..." literals vs 78 keyed calls).

## Findings

1. [BUG] `wpm list <query>` drops the query — dispatch passes no positional
   while `search` forwards it; guide §4.4/§4.5 promise filtering.
   (wpm.cpp:4051 vs 4083-4084)
2. [BUG] i18n custom_help stale: i18n_batch.py:132 carries an
   `install --from <manifest>` help line absent from print_usage()
   (wpm.cpp:3893-3955); plus a rewrapped --from description
   (i18n_batch.py:153-154). Running scripts/sync_wpm_help.py fixes both.
3. [DOC-MISMATCH] `source region` implemented and in --help but absent from
   guide §4.13. (wpm.cpp:3240,3905)
4. [DOC-MISMATCH] guide §5 Global Options omits -y/--yes, --proxy, --from
   (all in --help; wpm.cpp:62-75).
5. [DOC-MISMATCH] `update|upgrade coreutils` self-update alias undocumented
   (wpm.cpp:4112).
6. [DOC-MISMATCH] guide §6 says official-cn is "reserved, not populated";
   code ships it populated (wpm.cpp:127-135).
7. [DOC-MISMATCH] install state is receipt-based (.wpm/installed/<pkg>.json,
   wpm.cpp:2172-2258); guide §2/§4.1/§4.7 describe file-existence semantics
   and never mention receipts.
8. [DOC-MISMATCH] `export` vs `export --plain` documented as different;
   both print the same plain list (wpm.cpp:3397-3406).
9. [I18N-GAP] ~43 raw literals bypass keys. Top offenders: verify_sha256
   messages (1626-1638, guide-Troubleshooting-visible), copy errors
   (1863-1904, 2058-2119), preflight install messages (2236-2266),
   apply_update errors (2915-2947), index-status/source-list reports
   (3154-3222, 3229-3274), show_info report (3848-3888), list hints
   (3767-3780, 3389-3392).
10. [COSMETIC] version strings: file header 0.2.0, guide 0.3.0, kVersion
    0.4.0, User-Agent `WinuxCmd-WPM/0.2` (wpm.cpp:26,80,1327,1521).
11. [DOC-MISMATCH] exit-code scheme (0 ok/aborted/dry-run; 1 any failure)
    is consistent in code but undocumented in the guide.
12. [TEST-GAP] untested guide-promised behaviors: list <query>/--category,
    install --from (TOML/JSON/plain + conflict), --proxy + NO_PROXY,
    --force refusals, sha256 refusals, uninstall -y confirmation, update-index
    alias, source test, links remove, non-JSON outdated, shim-layout
    install/uninstall.
    Hidden `__apply-update` staying undocumented is intentional — fine.

Exit-code behavior in code is consistent and GNU-styled (lowercase "wpm: "
prefix, stderr) — no numeric oddities found.

## Top 10 fixes

1. Forward the query positional in `list` dispatch (wpm.cpp:4051).
2. Run scripts/sync_wpm_help.py and re-publish the catalog (pairs with the
   i18n repo VERSION bump).
3. Key the verify_sha256 + preflight_install_destinations messages.
4. Add -y/--proxy/--from to guide §5; document `source region` (§4.13) and
   the coreutils alias; correct §6 and the receipt model (§2/§4.1/§4.7).
5. Unify version strings and the User-Agent.
6. Unit tests: list query/--category; install --from; --force and sha256
   refusals; -y confirmation; shim layout.
