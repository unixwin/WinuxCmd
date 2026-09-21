# Performance roadmap — techniques mined from uutils/coreutils

Source: uutils/coreutils HEAD (2026-09-21 clone at
`tests/differential/.cache/upstream/uutils-coreutils`), plus their published
PGO setup (`util/build-pgo.sh`, `docs/src/performance.md`).

## Adopted: PGO build pipeline (this round)

MSVC PGO now wired in (`scripts/build-pgo.ps1` + `scripts/pgo-train.sh` +
CMake `WINUXCMD_PGO_INSTRUMENT` / `WINUXCMD_PGO_USE`):
instrumented build (/GL + /GENPROFILE) → training workloads (uutils
build-pgo.sh corpus ported verbatim + our differential corpus) →
/USEPROFILE final link.

Measured (200k-line fixture, wall clock):
- sort 0.51s → 0.42s (−18%; uutils published −17% for the same shape)
- nl 0.54s → 0.45s (−17%)
- fold −12%; size 4.49MB → 3.95MB (−12%)
- wc/cat/uniq at these sizes: noise

Pipeline pitfalls solved (they WILL bite again): /GENPROFILE needs /GL
objects (LNK1264); the instrumented exe needs pgort140.dll on PATH outside a
VS prompt (silent exit 127); /USEPROFILE reads <output>.pgd from the link
directory (copy pgd+pgc there; /PGD with quoted paths gets mangled through
ninja).

## Ranked backlog (impact × effort, from uutils source mining)

1. **cp: kernel copy** — regular-file copies via `CopyFile2`/`CopyFileExW`
   (uutils windows.rs:266 keeps the kernel path for ReFS CoW / SMB offload);
   we stream through iostreams (cp.cpp:1359). Large gain, trivial effort.
2. **wc -c O(1)** — regular file → return file_size without reading
   (uutils count_fast.rs:145-150); we always read (wc.cpp:311). ~15 lines.
3. **cat: 64-256 KiB block passthrough** + binary stdout `WriteFile` +
   `FILE_FLAG_SEQUENTIAL_SCAN` (uutils cat.rs:494 uses 64 KiB; we use 8 KiB
   via safePrint, cat.cpp:283). 2-3x passthrough.
4. **cat/wc/cut ASCII-run fast path** — scan to first byte ≥ 0x80, bulk-copy
   ASCII spans (uutils cut.rs:286); cat transform path is per-byte
   `stream.get` today (cat.cpp:294). 5-10x for -v/-n/-E on large files.
5. **Hash: CNG (`BCRYPT_SHA256_ALGORITHM`) or SHA-NI intrinsics** — our
   md5/sha256 paths still use deprecated CryptoAPI with 8 KiB buffers
   (md5sum.cpp:219); uutils uses SHA-NI-dispatching crates (sum.rs:466).
   3-8x SHA-256 on SHA-NI CPUs; also bump buffers to 64-256 KiB.
6. **sort: adaptive chunk sizing** — min(file×1.5, RAM/4) clamped
   (uutils buffer_hint.rs); we hardcode 8 MiB single-threaded and ignore
   --parallel (sort.cpp:1804). Far fewer merge passes on large sorts.
7. **uniq: streaming two-line window** — we slurp whole input
   (uniq.cpp:123); uutils keeps current/next lines only (uniq.rs:70).
   Fixes unbounded pipe memory.
8. **Dispatch-before-init** — i18n catalog load is lazy on first message,
   but console/env probing happens before dispatch; move anything
   non-essential after command resolution (uutils treats this as a design
   pillar, coreutils.rs:44-127).
9. **ls: FindFirstFileExW(FindExInfoBasic, LARGE_FETCH)** — batch attribute
   reads instead of per-entry std::filesystem queries.

## Explicitly not portable

splice / copy_file_range / fcopyfile / FICLONE (Linux/macOS kernel copy —
our `FSCTL_SET_SPARSE` sparse-copy path is already the Windows analogue and
more complete than needed); /proc-based size hints; pipe splice counting.

## Measurement discipline

`scripts/benchmark-command-parity.py` produces the JSON/Markdown wall-clock
probes; a benchmark baseline database + CI regression gate (adopting the
CodSpeed pattern uutils uses) is queued as part of the test-framework
iteration.
