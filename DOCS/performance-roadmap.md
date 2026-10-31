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
   [#1127-P4: evaluated and NOT taken — CopyFile2 also copies the
   source's attributes/timestamps, while the current path creates the
   destination fresh (dest mtime = copy time) unless --preserve asks
   for metadata; that observable delta, and its different interleave
   with the -f delete-and-retry fallback, cannot be equated by code
   reading alone. Revisit only with a Windows differential harness
   that fingerprints destination metadata.]
2. **wc -c O(1)** — regular file → return file_size without reading
   (uutils count_fast.rs:145-150); we always read (wc.cpp:311). ~15 lines.
   [#1127-P4: already landed before this round — count_file probes
   file_size when only bytes are requested.]
3. **cat: 64-256 KiB block passthrough** + binary stdout `WriteFile` +
   `FILE_FLAG_SEQUENTIAL_SCAN` (uutils cat.rs:494 uses 64 KiB; we use 8 KiB
   via safePrint, cat.cpp:283). 2-3x passthrough.
   [#1127-P4: passthrough block raised to 64 KiB. The live-stdin arms
   keep 8 KiB on purpose: there the read size is the interactive echo
   latency contract, not a throughput knob.]
4. **cat/wc/cut ASCII-run fast path** — scan to first byte ≥ 0x80, bulk-copy
   ASCII spans (uutils cut.rs:286); cat transform path is per-byte
   `stream.get` today (cat.cpp:294). 5-10x for -v/-n/-E on large files.
   [#1127-P4: landed for cat (verbatim runs around the per-byte
   renderer) and wc (bulk accounting of 0x21..0x7E spans). cut not
   audited in this round.]
5. **Hash: CNG (`BCRYPT_SHA256_ALGORITHM`) or SHA-NI intrinsics** — our
   md5/sha256 paths still use deprecated CryptoAPI with 8 KiB buffers
   (md5sum.cpp:219); uutils uses SHA-NI-dispatching crates (sum.rs:466).
   3-8x SHA-256 on SHA-NI CPUs; also bump buffers to 64-256 KiB.
   [#1127-P4: landed via CNG for md5/sha1/sha256/sha512 with 64 KiB
   feeds, mirroring wpm's existing BCrypt usage; BCrypt dispatches to
   SHA-NI internally, so no hand-written intrinsics were added.
   sha224/sha384/b2sum already used the portable engine.]
6. **sort: adaptive chunk sizing** — min(file×1.5, RAM/4) clamped
   (uutils buffer_hint.rs); we hardcode 8 MiB single-threaded and ignore
   --parallel (sort.cpp:1804). Far fewer merge passes on large sorts.
   [#1127-P4: landed — adaptive default min(input×1.5, RAM/4) clamped
   to [8 MiB, 1 GiB], and -S/--buffer-size is now actually honored
   (it was parsed, validated, and discarded before). --parallel still
   ignored, as before.]
7. **uniq: streaming two-line window** — we slurp whole input
   (uniq.cpp:123); uutils keeps current/next lines only (uniq.rs:70).
   Fixes unbounded pipe memory.
   [#1127-P4: the slurp framing was stale — input was already read
   line-by-line; the unbounded part was buffering every record of a
   group. Modes that never print later records (plain/-c/-d/-u) now
   keep the first record plus a counter; --group/-D keep the full
   group buffer because they print it.]
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
