# Differential report

Oracle: `cat (GNU coreutils) 8.32`

| Case | Result |
|---|---|
D:/repo/unixwin-winuxcmd/tests/differential/regressions/01-shred-size-zero.case	shred	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/02-split-no-overlap.case	split	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/03-binary-safe-text-tools.case	cat	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/04-dd-read-error.case	dd	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/05-tee-downstream-close.case	tee	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/06-xargs-command-limit.case	xargs	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/07-streaming-tools.case	cat	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/08-cp-empty-file.case	cp	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/09-cp-recursive-error.case	cp	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/10-diff-unified-header.case	diff	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/11-diff-crlf.case	diff	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/12-seq-limit.case	seq	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/13-tsort-graph.case	tsort	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/14-stat-mode-time.case	stat	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/15-tac-crlf.case	tac	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/16-crlf-consistency.case	sort	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/17-uniq-crlf.case	uniq	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/18-cut-no-delimiter.case	cut	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/19-fmt-width.case	fmt	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/20-test-file-time.case	test	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/21-id-group.case	id	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/22-dirname-basename-root.case	dirname	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/23-cp-link-option.case	cp	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/23-date-relative.case	date	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/24-chmod-short-mode.case	chmod	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/25-sed-preserve-attributes.case	sed	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/26-long-path.case	stat	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/27-xxd-reverse.case	xxd	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/28-mv-readonly.case	mv	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/29-join-j-alias.case	join	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/30-sdiff-output-file.case	sdiff	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/31-leading-dash-operands.case	printf	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/32-printf-dynamic-width.case	printf	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/33-printf-shell-quote.case	printf	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/34-test-logical-operators.case	test	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/35-ls-explicit-sort.case	ls	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/36-head-positive-count.case	head	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/37-numfmt-options.case	numfmt	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/38-pr-page-length.case	pr	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/39-unix2dos-no-final-newline.case	unix2dos	PASS
D:/repo/unixwin-winuxcmd/tests/differential/regressions/40-ptx-format.case	ptx	OUT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/41-stdbuf-line-mode.case	stdbuf	EXIT_DIFF
D:/repo/unixwin-winuxcmd/tests/differential/regressions/42-locale-keyword.case	locale	PASS

## Summary

- PASS: 36/43
- EXIT_DIFF: 1/43
- OUT_DIFF: 6/43

## By command

| Command | PASS | Executed | Rate |
|---|---:|---:|---:|
| cat | 2 | 2 | 100.0% |
| chmod | 1 | 1 | 100.0% |
| cp | 3 | 3 | 100.0% |
| cut | 1 | 1 | 100.0% |
| date | 1 | 1 | 100.0% |
| dd | 0 | 1 | 0.0% |
| diff | 1 | 2 | 50.0% |
| dirname | 1 | 1 | 100.0% |
| fmt | 0 | 1 | 0.0% |
| head | 1 | 1 | 100.0% |
| id | 1 | 1 | 100.0% |
| join | 1 | 1 | 100.0% |
| locale | 1 | 1 | 100.0% |
| ls | 1 | 1 | 100.0% |
| mv | 1 | 1 | 100.0% |
| numfmt | 1 | 1 | 100.0% |
| pr | 1 | 1 | 100.0% |
| printf | 3 | 3 | 100.0% |
| ptx | 0 | 1 | 0.0% |
| sdiff | 1 | 1 | 100.0% |
| sed | 1 | 1 | 100.0% |
| seq | 1 | 1 | 100.0% |
| shred | 1 | 1 | 100.0% |
| sort | 1 | 1 | 100.0% |
| split | 1 | 1 | 100.0% |
| stat | 1 | 2 | 50.0% |
| stdbuf | 0 | 1 | 0.0% |
| tac | 1 | 1 | 100.0% |
| tee | 1 | 1 | 100.0% |
| test | 2 | 2 | 100.0% |
| tsort | 0 | 1 | 0.0% |
| uniq | 1 | 1 | 100.0% |
| unix2dos | 1 | 1 | 100.0% |
| xargs | 1 | 1 | 100.0% |
| xxd | 1 | 1 | 100.0% |

Cases executed: 43
PASS: 36
SKIP: 0
Differences: 7
