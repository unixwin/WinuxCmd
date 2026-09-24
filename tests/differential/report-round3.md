# Differential report

Oracle: `cat (GNU coreutils) 9.4`

| Case | Result |
|---|---|
corpus/cat/basic.case	cat	PASS
corpus/head/positive-offset.case	head	PASS
corpus/split/no-overlap.case	split	PASS
regressions/01-shred-size-zero.case	shred	PASS
regressions/02-split-no-overlap.case	split	PASS
regressions/03-binary-safe-text-tools.case	cat	PASS
regressions/04-dd-read-error.case	dd	PASS
regressions/05-tee-downstream-close.case	tee	PASS
regressions/06-xargs-command-limit.case	xargs	PASS
regressions/07-streaming-tools.case	cat	PASS
regressions/08-cp-empty-file.case	cp	PASS
regressions/09-cp-recursive-error.case	cp	PASS
regressions/10-diff-unified-header.case	diff	PASS
regressions/11-diff-crlf.case	diff	PASS
regressions/12-seq-limit.case	seq	PASS
regressions/13-tsort-graph.case	tsort	PASS
regressions/14-stat-mode-time.case	stat	PASS
regressions/15-tac-crlf.case	tac	PASS
regressions/16-crlf-consistency.case	sort	PASS
regressions/17-uniq-crlf.case	uniq	PASS
regressions/18-cut-no-delimiter.case	cut	PASS
regressions/19-fmt-width.case	fmt	PASS
regressions/20-test-file-time.case	test	PASS
regressions/21-id-group.case	id	KNOWN_DIFF
regressions/22-dirname-basename-root.case	dirname	PASS
regressions/23-cp-link-option.case	cp	KNOWN_DIFF
regressions/23-date-relative.case	date	PASS
regressions/24-chmod-short-mode.case	chmod	PASS
regressions/25-sed-preserve-attributes.case	sed	PASS
regressions/26-long-path.case	stat	PASS
regressions/27-xxd-reverse.case	xxd	PASS
regressions/28-mv-readonly.case	mv	PASS
regressions/29-join-j-alias.case	join	PASS
regressions/30-sdiff-output-file.case	sdiff	PASS
regressions/31-leading-dash-operands.case	printf	PASS
regressions/32-printf-dynamic-width.case	printf	PASS
regressions/33-printf-shell-quote.case	printf	PASS
regressions/34-test-logical-operators.case	test	PASS
regressions/35-ls-explicit-sort.case	ls	PASS
regressions/36-head-positive-count.case	head	PASS
regressions/37-numfmt-options.case	numfmt	PASS
regressions/38-pr-page-length.case	pr	PASS
regressions/39-unix2dos-no-final-newline.case	unix2dos	PASS
regressions/40-ptx-format.case	ptx	PASS
regressions/41-stdbuf-line-mode.case	stdbuf	PASS
regressions/42-locale-keyword.case	locale	PASS

## Summary

- PASS: 44/46
- KNOWN_DIFF: 2/46

## By command

| Command | PASS | Executed | Rate |
|---|---:|---:|---:|
| cat | 3 | 3 | 100.0% |
| chmod | 1 | 1 | 100.0% |
| cp | 2 | 3 | 66.7% |
| cut | 1 | 1 | 100.0% |
| date | 1 | 1 | 100.0% |
| dd | 1 | 1 | 100.0% |
| diff | 2 | 2 | 100.0% |
| dirname | 1 | 1 | 100.0% |
| fmt | 1 | 1 | 100.0% |
| head | 2 | 2 | 100.0% |
| id | 0 | 1 | 0.0% |
| join | 1 | 1 | 100.0% |
| locale | 1 | 1 | 100.0% |
| ls | 1 | 1 | 100.0% |
| mv | 1 | 1 | 100.0% |
| numfmt | 1 | 1 | 100.0% |
| pr | 1 | 1 | 100.0% |
| printf | 3 | 3 | 100.0% |
| ptx | 1 | 1 | 100.0% |
| sdiff | 1 | 1 | 100.0% |
| sed | 1 | 1 | 100.0% |
| seq | 1 | 1 | 100.0% |
| shred | 1 | 1 | 100.0% |
| sort | 1 | 1 | 100.0% |
| split | 2 | 2 | 100.0% |
| stat | 2 | 2 | 100.0% |
| stdbuf | 1 | 1 | 100.0% |
| tac | 1 | 1 | 100.0% |
| tee | 1 | 1 | 100.0% |
| test | 2 | 2 | 100.0% |
| tsort | 1 | 1 | 100.0% |
| uniq | 1 | 1 | 100.0% |
| unix2dos | 1 | 1 | 100.0% |
| xargs | 1 | 1 | 100.0% |
| xxd | 1 | 1 | 100.0% |

Cases executed: 46
PASS: 44
KNOWN_DIFF: 2
SKIP: 0
Differences: 0
