# WinuxCmd vs Microsoft Coreutils Behavior Matrix

Generated from curated behavior probes over shared commands.

Generated at: 2026-06-10T23:25:12.5772494+08:00
Winux directory: C:\Users\cmx\repo\WinuxCmd\build-gnu-maint-vs-bigobj
Microsoft directory: C:\Users\cmx\repo\_winget_coreutils_install\bin

## Summary

| Metric | Value |
| --- | ---: |
| Probe count | 9 |
| Matching probes | 9 |
| Differing probes | 0 |

## Probe Summary

| Probe | Command | Result | Stdout | Stderr | Exit | Filesystem |
| --- | --- | --- | --- | --- | --- | --- |
| cat-file | cat | same | True | True | True | True |
| cp-file | cp | same | True | True | True | True |
| ls-basic | ls | same | True | True | True | True |
| head-two-lines | head | same | True | True | True | True |
| tail-two-lines | tail | same | True | True | True | True |
| sort-file | sort | same | True | True | True | True |
| grep-line-number | grep | same | True | True | True | True |
| find-top-files | find | same | True | True | True | True |
| xargs-basic | xargs | same | True | True | True | True |

## Probe Details

### cat-file

Command: ``cat sample.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
alpha
beta
alpha beta
gamma
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
alpha
beta
alpha beta
gamma
```
Microsoft stderr:
```text

```

### cp-file

Command: ``cp sample.txt copies/copied.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text

```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text

```
Microsoft stderr:
```text

```

### ls-basic

Command: ``ls -1``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
alpha
beta
copies
sample.txt
sortable.txt
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
alpha
beta
copies
sample.txt
sortable.txt
```
Microsoft stderr:
```text

```

### head-two-lines

Command: ``head -n 2 sample.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
alpha
beta
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
alpha
beta
```
Microsoft stderr:
```text

```

### tail-two-lines

Command: ``tail -n 2 sample.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
alpha beta
gamma
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
alpha beta
gamma
```
Microsoft stderr:
```text

```

### sort-file

Command: ``sort sortable.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
apple
banana
pear
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
apple
banana
pear
```
Microsoft stderr:
```text

```

### grep-line-number

Command: ``grep -n alpha sample.txt``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
1:alpha
3:alpha beta
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
1:alpha
3:alpha beta
```
Microsoft stderr:
```text

```

### find-top-files

Command: ``find . -maxdepth 1 -type f -print``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
.\sample.txt
.\sortable.txt
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
.\sample.txt
.\sortable.txt
```
Microsoft stderr:
```text

```

### xargs-basic

Command: ``xargs -n 1 echo``
stdin: ``alpha beta\ngamma\n``
Result: same
Matches: stdout=True, stderr=True, exit=True, filesystem=True

Winux exit: 0
Winux stdout:
```text
alpha
beta
gamma
```
Winux stderr:
```text

```
Microsoft exit: 0
Microsoft stdout:
```text
alpha
beta
gamma
```
Microsoft stderr:
```text

```

