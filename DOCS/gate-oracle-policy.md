# GNU master differential: oracle sourcing, anchors and adjudication

Scope: the `oracle` job of `.github/workflows/gnu-master-differential.yml`
and `tests/differential/fetch-gnu-master.sh`. The `compare`, `baseline` and
`baseline-growth` jobs are not affected by this policy.

## The failure this replaces

The oracle job pinned coreutils master (`GNU_REVISION`) and fetched its
`gnulib` submodule straight from the canonical host:

```
git submodule update --init --depth=1
```

That host intermittently aborts in the middle of the packfile. Observed on
run 43454991460 (job 131986648321), 2026-11-13:

```
Cloning into '/home/runner/work/_temp/coreutils/gnulib'...
error: RPC failed; HTTP 500 curl 22 The requested URL returned error: 500
fatal: expected 'packfile'
fatal: Fetched in submodule path 'gnulib', but it did not contain 106e9b2384d08a1696fcbd40cbab52237943f208.
      Direct fetching of that commit failed.
##[error]Process completed with exit code 128.
```

7 of the last 58 completed differential runs failed exactly here; every one
skipped `compare`, so the PR received no differential signal. The commit was
available the whole time from other transports - the failure is transport
only, which is why it is a candidate for sourcing and retry, not for
adjudication on its own.

## Sources and precedence

The anchor file `rust2/gate/vendor/gnulib.anchor.json` is the single source of
truth for both revisions, the vendored payload and its digests. Nothing in the
script hardcodes a sha.

| order | transport | coreutils | gnulib | verification |
|---|---|---|---|---|
| 1 | upstream git | `git.savannah.gnu.org/git/coreutils.git` | `git.savannah.gnu.org/git/gnulib.git` | `git rev-parse HEAD` == anchored revision; tree fingerprint |
| 2 | GitHub mirror | `github.com/coreutils/coreutils.git` | `github.com/coreutils/gnulib.git` | same |
| 3 | cgit snapshot | - | `git.savannah.gnu.org/cgit/gnulib.git/snapshot/gnulib-<sha>.tar.gz` | anchored **tree fingerprint** after extraction |
| 4 | vendored archive | - | `rust2/gate/vendor/gnulib-<sha>.tar.gz` | anchored **sha256** plus tree fingerprint |

For coreutils the GitHub mirror is tried first because it is the transport the
oracle has always used successfully; savannah is the fallback. For gnulib the
canonical host is tried first so that a healthy upstream day is used directly,
and the vendored copy is last.

Only gnulib is vendored. The incident was gnulib-specific, coreutils is
fetched from two independent hosts, and a second ~50 MB checkout is not worth
carrying until a coreutils transport failure is actually observed.

A source that cannot prove the anchored revision is **rejected**, never
silently accepted:

* a git worktree whose `HEAD` differs is a stale mirror, not a transient
  error, so it is not retried (`rc=2`);
* an archive whose bytes differ from the anchored sha256 is rejected (the
  vendored copy has an anchored digest; a cgit snapshot legitimately has
  different bytes, so it is accepted on a matching tree fingerprint);
* if every source fails, the job fails. There is no "use whatever the mirror
  had".

The tree fingerprint is sha256 over the sorted `path<TAB>size<TAB>sha256`
rows of every regular file (symlinks recorded by target), with `.git`
excluded. It is stable across git checkouts and tarball extractions of the
same commit.

The selected source and both revisions are written to
`gnu-master-sources.json` and uploaded with `gnu-master-results.json`, so an
adjudication can quote exactly which transport produced the record.

## Retry policy (implemented in the script, not in YAML)

Constants in `tests/differential/fetch-gnu-master.sh`, overridable by
environment for local debugging:

| knob | default | meaning |
|---|---|---|
| `RETRY_ATTEMPTS` | 2 | attempts per source |
| `RETRY_BACKOFF_SECONDS` | `5 20` | sleep before attempt 2, 3, … |
| `ATTEMPT_TIMEOUT` | 180 s | hard ceiling per git/curl attempt (`timeout -k 15`) |
| `GIT_LOW_SPEED_LIMIT` / `GIT_LOW_SPEED_TIME` | 1000 B/s / 60 s | abort a stalled transfer |
| `SOURCE_BUDGET_SECONDS` | 600 s | total budget for **network** sources; the vendored source is local and never gated |

Transport errors are retried with backoff. Revision or fingerprint
mismatches are not retried. Once the budget is exhausted the network sources
are abandoned and the local vendored archive is used, so a bad upstream day
degrades to "slow but green" instead of "30-minute job timeout".

## When a red oracle leg may be adjudicated manually

A red `oracle` job means "no differential signal for this PR". It does not by
itself mean the PR regressed anything, and it must not be silenced by calling
the leg green. A maintainer may record an adjudication (merge without the
signal) only when **all** of the following hold:

1. The failing step is `Build the pinned GNU master source` (sourcing or the
   upstream build), not `Record GNU byte-level results` and not `compare`.
2. The log proves a transport or upstream defect: DNS/TLS/connect timeout,
   HTTP 4xx/5xx, packfile truncation, or an upstream tree that does not build
   at the anchored revision. "The job was slow" or "it worked yesterday" is
   not evidence.
3. `gnu-master-sources.json` shows the attempted source order; if the
   vendored archive also failed its sha256/fingerprint check, stop - that is a
   repository integrity problem, not an infrastructure flake.
4. The anchored revisions in `gnulib.anchor.json` and the workflow
   `GNU_REVISION` are unchanged by the PR. If the PR changes either, there is
   no precedent to reuse; a fresh green oracle run is required.
5. The adjudication record names the run id, the job id, the failing step, the
   verbatim error line, and the anchored `coreutils_revision` /
   `gnulib_revision`. It is attached to the PR, not to a local note.
6. A re-run is attempted after the upstream recovers, before the PR is
   reported as "gate passing". Until that run is green, the correct status is
   "oracle unavailable, adjudicated", never "oracle passed".

Never adjudicable:

* any mismatch reported by `compare` (that is a real byte difference);
* `oracle revision does not match the requested GNU revision` or
  `oracle corpus does not match the checked-out cases` from
  `master-oracle.py` - these are the anchor guards working;
* a revision or tree-fingerprint mismatch from the fetch script;
* a missing or malformed `gnu-master-results.json`/`gnu-master-sources.json`;
* a local run that used a gnulib tree whose commit cannot be shown to equal
  the anchor.

## Anti-stale rules

These exist because a stale gate result once overwrote a fresh one and
invalidated gate01 (`DOCS/2.0-HANDOVER.md`, "零、续作检查点"). The same class
of mistake here would be reusing an oracle record from a different revision
or letting a later source silently replace an earlier, verified tree.

* An oracle record is valid only for the exact pair
  (`revision`, `cases_sha256`). `master-oracle.py --compare` already refuses a
  record when either differs; do not bypass it.
* The fetch script removes and re-materializes the destination tree before
  every attempt. There is no incremental reuse of a previously fetched
  gnulib.
* Source selection is first-success-wins with verification; a later,
  lower-priority source never overwrites a verified tree.
* Adjudications must quote the provenance artifact of the run they excuse.
  A record from a previous run, a different runner, or a different source is
  not evidence for the current run.
* The oracle artifact is uploaded from the same job that built the binaries;
  the `compare` job consumes the artifact and re-checks the revision, so a
  record cannot be substituted across runs without tripping the revision
  check.

## Re-vendoring (when `GNU_REVISION` advances)

1. Resolve the new gnulib commit from the new coreutils revision
   (`git ls-tree <coreutils-rev> gnulib`).
2. Download `https://codeload.github.com/coreutils/gnulib/tar.gz/<gnulib-sha>`
   and save it as
   `rust2/gate/vendor/gnulib-<gnulib-sha>.tar.gz`.
3. Compute `vendored_sha256` (sha256 of the archive), `vendored_size`, and
   `tree_sha256` (the fingerprint above, computed on Linux after
   `tar -xzf <archive> --strip-components=1`; do not compute it on a
   case-insensitive filesystem).
4. Update `rust2/gate/vendor/gnulib.anchor.json` **in the same commit** as the
   new workflow `GNU_REVISION`, and delete the old archive so the repository
   does not carry two vendored trees.
5. Run the oracle job (or `fetch-gnu-master.sh` locally) and confirm
   `gnu-master-sources.json` reports the intended source; then record the new
   differential results.
