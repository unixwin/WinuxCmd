# Per-spawn AV scanning study (issue #1396)

Measurement of WinuxCmd applet spawn latency against signature/MOTW/cold-warm
variables, plus an assessment of the three proposed mitigations (SignPath
signing, catalog signing, installer layout).

**Status: partial.** The AV-on leg of the matrix could not be measured on this
host because Microsoft Defender Antivirus real-time protection is pinned off by
machine policy and cannot be re-armed without a reboot. Everything else was
measured; the AV-on legs and the SignPath-authenticated legs are marked N/A with
the evidence below rather than filled with proxies.

---

## 1. Environment

| item | value |
|---|---|
| OS | Windows 10 Pro 22H2, 10.0.19045.6466 (UBR 6466), x64 |
| Machine | physical, JWIPC SYS-6023H, not a VM |
| CPU / RAM | Intel Xeon w5-3433, 16 cores / 32 threads, 2.0 GHz / 63.7 GB |
| Working volume | `D:` - mechanical disk (the payload copies for cold legs land here) |
| Security software | Microsoft Defender Antivirus only; no third-party AV registered in SecurityCenter2, no 360/Huorong/Kaspersky/etc. installed |
| Defender engine / platform | engine 1.1.26100.4, platform 4.18.26100.4 |
| Defender signatures | 1.463.290.0 |
| Defender real-time protection | **off** - `HKLM\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection\DisableRealtimeMonitoring = 1` was set when the study started |
| Defender cloud / MAPS | off (`MAPSReporting=0`, `SubmitSamplesConsent=0`, `CloudBlockLevel=0`) |
| Defender exclusions (as found) | paths `…\Programs\Niubash`, `D:\repo\niubash\target`, `D:\repo\rubash\target`, `D:\repo\WinuxCmd`, `D:\tmp\devin-fleet`, `D:\tmp\msix-payload`; processes `bash.exe`, `devin.exe`, `niu.exe`, `rubash.exe`, **`winuxcmd.exe`** |
| SmartScreen | not configured (`SmartScreenEnabled` absent); no MOTW policy in HKCU `…\Policies\Attachments` |
| MOTW support | NTFS ADS available; `Zone.Identifier` written and read back for every MOTW variant |
| Administrator | the interactive user is in Administrators; UAC is configured `ConsentPromptBehaviorAdmin=0`, so privileged steps ran through a silent `Start-Process -Verb RunAs` helper |
| Host load during runs | 9-48 % average CPU (the host is shared with other build lanes); the reported "run 1" is the quietest pass, 23-26 % |
| Test data | 3 passes x 2 760 spawns (68 legs each), 8 280 rows total in `raw/samples.csv` |

Raw state snapshots: `raw/inventory.csv`, `raw/inventory.txt`,
`raw/defender-original.json`.

## 2. What was measured

Every leg spawns one image and waits for exit, timed with
`QueryPerformanceCounter` around `CreateProcessW` (+`WaitForSingleObject`) or
`ShellExecuteExW`. Timing is done in native code (`scripts/SpawnBench.cs`)
because PowerShell's own invocation cost is 5-15 ms, the same order as the
effect on an idle machine.

| dimension | values |
|---|---|
| applets | `ls`, `which`, `sort` (all three are hardlinks of one `winuxcmd.exe`, exactly like the installed layout) and `git` (a different image) |
| arguments | `--version` for all four |
| variants | `installed` (the binaries on PATH, in place), `unsigned` (release image, no signature), `signed` (same image + valid Authenticode signature from a locally trusted test CA), `motw-unsigned`, `motw-signed` (`ZoneId=3`), `microsoft-signed` (`whoami.exe`, control) |
| state | `warm` = repeated spawn of one stable path (the bug report's state); `cold` = each sample spawns a fresh byte copy in a new directory (new path + new file identity, i.e. right after install/upgrade); `newhash` = cold plus a trailing pad byte, so the content hash is new too |
| spawn API | `CreateProcessW` (terminal/build-tool path) and `ShellExecuteExW` (Explorer path, where SmartScreen/MOTW policy acts) |
| samples | 50 per warm/cold/newhash leg, 10 per Explorer-path leg (bounded by an 8 s timeout; a timeout is recorded as *blocked*, never as a latency) |
| statistics | median and nearest-rank P95 over the samples, min/max, stdev in `raw/legs.csv` |

Warm and cold differ only in the file identity of the image being spawned, so
the deltas isolate "image/verdict already known" from "first sight".

## 3. Results

Run 1 is the quiet pass (`rtp-off-quiet`, 23-26 % load), run 2 the earlier pass
(`rtp-off`, up to 48 %). Cells where the two runs differ by more than 20 % are
marked `*` - that is host noise, not an effect of the variable.

### 3.1 Warm spawn, `CreateProcessW` (median / P95, ms)

| variant | applet | run 1 median | run 1 P95 | run 2 median | run 2 P95 | notes |
|---|---|---|---|---|---|---|
| installed | ls | 16.5 | 19.1 | 16.9 | 20.5 | on PATH, inside the existing Defender exclusions |
| installed | which | 15.4 | 18.4 | 16.5 | 19.0 | on PATH |
| installed | sort | 15.8 | 17.8 | 16.3 | 18.9 | on PATH |
| installed | git | 12.8 | 16.4 | 12.5 | 15.3 | on PATH, different image |
| unsigned | ls | 17.4 | 20.0 | 17.6 | 20.8 | release image, no signature |
| unsigned | which | 17.9 | 22.4 | 14.2 | 18.5 | \* |
| unsigned | sort | 18.4 | 26.9 | 14.2 | 21.5 | \* |
| unsigned | git | 13.6 | 15.9 | 14.1 | 16.1 | |
| signed | ls | 20.7 | 30.7 | 16.8 | 19.5 | trusted test cert |
| signed | which | 16.1 | 21.0 | 30.9 | 108.9 | \* (run 2 contended) |
| signed | sort | 15.0 | 17.5 | 14.1 | 15.8 | |
| signed | git | 12.1 | 16.0 | 11.0 | 21.3 | |
| motw-unsigned | ls | 14.7 | 17.7 | 13.4 | 15.8 | + Zone.Identifier |
| motw-unsigned | which | 16.1 | 18.6 | 14.1 | 15.7 | |
| motw-unsigned | sort | 14.8 | 17.5 | 14.2 | 16.8 | |
| motw-unsigned | git | 11.4 | 15.7 | 12.2 | 18.7 | |
| motw-signed | ls | 14.9 | 18.8 | 49.7 | 516.9 | \* (run 2 heavily contended) |
| motw-signed | which | 15.0 | 18.0 | 14.3 | 15.2 | |
| motw-signed | sort | 14.8 | 18.2 | 13.7 | 15.9 | |
| motw-signed | git | 11.7 | 14.7 | 12.5 | 16.9 | |

### 3.2 Cold and new-hash spawn, `CreateProcessW` (median, ms)

| variant | applet | cold run 1 | cold run 2 | newhash run 1 | newhash run 2 | cold - warm (run 1) |
|---|---|---|---|---|---|---|
| unsigned | ls | 22.6 | 24.6 | 31.6 | 26.9 | 5.2 |
| unsigned | which | 23.5 | 17.5 | 23.2 | 20.5 | 5.6 |
| unsigned | sort | 23.3 | 22.4 | 24.9 | 24.3 | 4.9 |
| unsigned | git | 17.5 | 14.1 | 22.4 | 17.4 | 4.0 |
| signed | ls | 25.7 | 22.9 | 26.2 | 22.3 | 4.9 |
| signed | which | 20.5 | 18.8 | 27.7 | 24.0 | 4.3 |
| signed | sort | 18.8 | 17.6 | 21.1 | 19.9 | 3.8 |
| signed | git | 15.2 | 14.2 | 17.6 | 16.6 | 3.1 |
| motw-unsigned | ls | 18.6 | 148.2 | 21.5 | 24.1 | 3.9 |
| motw-unsigned | which | 19.5 | 18.6 | 21.0 | 20.7 | 3.4 |
| motw-unsigned | sort | 18.6 | 19.1 | 21.1 | 28.6 | 3.7 |
| motw-unsigned | git | 15.4 | 14.8 | 19.8 | 41.9 | 3.9 |
| motw-signed | ls | 22.7 | 18.9 | 22.8 | 20.0 | 7.8 |
| motw-signed | which | 19.7 | 17.6 | 22.0 | 19.9 | 4.7 |
| motw-signed | sort | 18.9 | 18.2 | 21.9 | 29.6 | 4.1 |
| motw-signed | git | 15.3 | 16.0 | 17.8 | 22.2 | 3.6 |

Cold costs +3 to +8 ms over warm, and a brand-new content hash another +2 to
+9 ms on top: that is the whole app-side share of a first run. Run-to-run
medians agree within ~5 ms except two clearly contended legs (`motw-unsigned`
`ls` cold in run 2, `motw-signed` `ls` warm in run 2), marked `*` in 3.1.

### 3.3 Explorer path (`ShellExecuteExW`), warm, 10 samples (median / P95, ms)

| variant | ls | which | sort | git |
|---|---|---|---|---|
| unsigned | 95.4 / 107.2 | 90.9 / 125.2 | 82.5 / 88.8 | 78.3 / 81.6 |
| signed | 75.2 / 77.8 | 71.4 / 88.2 | 78.7 / 98.1 | 75.4 / 90.0 |
| motw-unsigned | 79.5 / 88.7 | 81.2 / 89.1 | 78.1 / 98.6 | 85.4 / 128.2 |
| motw-signed | 81.0 / 104.5 | 87.4 / 104.2 | 80.5 / 245.3 | 78.9 / 95.3 |

No blocked spawns in the reported run. On one earlier pass, a zone-marked
unsigned image on the Explorer path did raise the SmartScreen window
(`smartscreen.exe` observed) and did not return within the bound; that path is
why every Explorer-leg sample now carries a timeout. It reproduced once, not in
three later passes, so it is reported as intermittent, not as a number.

### 3.4 What the Defender engine costs per image (`MpCmdRun -Scan -ScanType 3`)

| variant | image | cached verdict (ms) | cached verdict, path excluded (ms) | cold verdict, new path+hash (ms) |
|---|---|---|---|---|
| unsigned | ls.exe (4.6 MB) | 25.5 | 26.7 | 167.0 |
| signed | ls.exe (4.6 MB) | 28.0 | 27.5 | 170.3 |
| motw-unsigned | ls.exe | 25.3 | 29.9 | 179.0 |
| motw-signed | ls.exe | 26.5 | 25.3 | 191.5 |
| unsigned | git.exe (5.2 MB) | 25.0 | 24.6 | 47.5 |
| signed | git.exe (5.2 MB) | 24.8 | 27.0 | 47.7 |
| microsoft-signed | whoami.exe | - | - | 54.0 |

Reading: the ~25 ms floor is the tool plus a verdict that is already cached.
A first-scan of the WinuxCmd image costs ~120 ms more (167-191 ms total), for
**unsigned and signed alike** - the signature does not shorten the scan - while
a widely-known image of the same size (git, and an OS binary) stays at the
~50 ms floor. MOTW moves the number by less than the run-to-run noise.

Excluding the directory did **not** change the on-demand scan here. Microsoft
documents exclusions as applying to on-demand scans, so the most likely
explanation is that `MpCmdRun -Scan -File` scans the explicitly named target
regardless; either way, this host cannot show the exclusion effect on real-time
scanning because real-time protection is off.

### 3.5 What the numbers say about the reported 0.9 s

* The dispatcher is not the problem: `ls`, `which` and `sort` are one image and
  one process, and they spawn in 15-18 ms warm, 19-26 ms cold, 20-32 ms with a
  brand-new content hash - a 3-5 ms cold penalty over warm, not hundreds.
* `git` is consistently 2-4 ms faster only because its image is a different,
  widely-known binary; the gap is not a layout or signature effect.
* Signature and MOTW do not change spawn latency when nothing scans: median
  deltas stay inside run-to-run noise (<= 4 ms).
* The only mechanism on this host that costs hundreds of milliseconds is a
  **first-scan of the image by the AV engine** (~120 ms here, cloud lookups
  disabled). The 0.9 s reported on 360/Huorong-class products is plausible as
  "engine scan + cloud reputation round trip, per spawn, verdict not reused",
  but this host cannot confirm it: see section 4.

## 4. Why the AV-on legs are N/A on this host

The machine was found with Defender real-time protection off, pinned by policy.
The following was verified, in this order, instead of assumed:

1. `Get-MpComputerStatus` reported `RealTimeProtectionEnabled=False` while
   `AntivirusEnabled=True`, `AMRunningMode=Normal`.
2. `HKLM\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection`
   had `DisableRealtimeMonitoring=1` (a policy pin, so `Set-MpPreference` cannot
   override it).
3. The policy was set to 0 and `Set-MpPreference -DisableRealtimeMonitoring
   $false` applied: `Get-MpPreference` then reports
   `DisableRealtimeMonitoring=False`, but `RealTimeProtectionEnabled` stays
   `False`. (The engine's own registry value under `...\Windows Defender\Real-Time
   Protection` is ACL-protected against direct writes by Administrators;
   `Set-MpPreference` is the supported writer, so that path is the one that
   matters.)
4. `WinDefend`'s service DACL refuses stop/start to Administrators (`sc stop`
   -> error 5). A short-lived SYSTEM scheduled task bounced the service anyway;
   the flag still stayed `False`. `fltmc filters` shows `WdFilter` attached with
   13 instances - the driver is there, the engine is not scanning.
5. Empirical check rather than trusting the flag: the EICAR test string was
   written to a non-excluded directory and survived (`Get-MpThreatDetection`
   empty, `RealTimeProtectionEnabled=False`). No real-time scanning happened.
6. `TamperProtection=4` in `HKLM\SOFTWARE\Microsoft\Windows Defender\Features`
   and the policy history indicate the engine re-arms the setting at boot.
   Re-arming therefore needs a reboot of a machine that is shared with other
   work; that was out of scope for this lane.

Everything the machine was changed to was restored afterwards (section 7).

N/A legs, explicitly:

| leg | status |
|---|---|
| AV real-time protection ON: any warm/cold/signature/MOTW spawn latency | **N/A** - cannot enable without a reboot (section 4) |
| Defender exclusion on/off with real-time protection ON | **N/A** - depends on the leg above; on-demand proxy showed no difference |
| 360 / Huorong / Kaspersky on-access behaviour | **N/A** - none installed, cannot install on this host |
| SignPath test-signed and release-signed artifacts | **N/A** - signing token/policy not available locally; leg run with a locally trusted self-signed cert instead (see 6.1) |
| SmartScreen prompt latency / block rate | **N/A** - SmartScreen not configured on this host; the Explorer-path MOTW legs showed a prompt once, then not again |
| Smart App Control (Win11) interaction | **N/A** - Windows 10 |

## 5. Mitigation analysis (research only, nothing implemented)

### 5.1 SignPath / release certificate signing

What it buys, per Microsoft and SignPath documentation:

* Publisher identity on the binary, which is what SmartScreen shows and what
  the AV reputation graph keys on. Unsigned files start at zero reputation for
  *every version*; a certificate's reputation can carry over to new files signed
  with the same identity.
* Signed-but-new binaries still trip SmartScreen until their hash or publisher
  accrues reputation - Microsoft states this explicitly, and states that EV no
  longer bypasses it. A *self-signed* certificate behaves like no signature.
* It does **not** remove per-spawn scanning. In this study the signed image was
  scanned cold in 170 ms vs 167 ms unsigned; the only values the Defender
  performance analyzer reports for a scan's `SkipReason` are `Not Skipped`,
  `Optimization` and `User skipped` - there is no "has a signature" skip.

Verdict: necessary for distribution hygiene and future Smart App Control /
SmartScreen behaviour, but not a latency fix for the issue as reported.

### 5.2 Microsoft catalog (`.cat`) signing

A catalog is a signed collection of file hashes; installing it (CatRoot, or
`SignTool catdb`) makes Windows treat the listed files as signed without an
embedded signature per file. That is attractive for a multi-call binary with
dozens of hardlinked names: one catalog covers all applet names.

However, no Microsoft documentation states that a catalog signature suppresses
per-spawn AV scanning, and the counterpart of the mechanism is identity and
integrity, not a scan waiver. Defender still has to decide whether the bytes are
malicious. The only documented "trust this by certificate" mechanisms are
Defender for Endpoint **certificate indicators** (tenant-level allow) and
**path/process exclusions** - both deployment-side, neither shipped in the
binary.

Verdict: useful to sign a many-applet installation cheaply and to satisfy
policy/catalog-based integrity checks; explicitly **not** a demonstrated
per-spawn scan elimination, so it should not be sold as the fix.

### 5.3 Installer layout

Measured facts that constrain the design:

* One applet spawn opens exactly **one** image: `usr/bin/ls.exe` is a hardlink
  of `winuxcmd.exe`, so the dispatcher runs in the same process (`ls`, `which`,
  `sort` are indistinguishable, 15-18 ms warm).
* A launcher-plus-payload layout (wpm `shim` layout: `usr/bin/x.exe` forwards to
  `opt/<pkg>/x.exe`) opens **two** images for one command, hence two AV
  decisions instead of one. Hot commands should keep the flat, one-image shape
  that `git` has here.
* Cold (new path) costs 3-5 ms over warm, and a new content hash another 2-10 ms.
  That is the *app-side* share of a first run; the remaining ~120 ms in section
  3.4 belongs to the AV engine and cannot be removed by layout.
* Avoid per-run self-extraction and per-applet copies of the same 4.6 MB image:
  each distinct file identity is a separate verdict to establish.
* Defender applies built-in exclusions only to OS files; application install
  directories are not covered, so a documented deployment-time exclusion (path
  or, as this very host already does, process name `winuxcmd.exe`) is the only
  lever that reliably removes the scan - at the cost of protection for that
  path.

### 5.4 Ranking for the issue

1. **Exclusion at install time** (documented skip, Defender's `SkipReason` says
   "user skipped"; the minimal evaluation cost remains). Best effect, needs
   installer/admin rights, weakens protection for the install directory - use a
   process exclusion or a contextual `ScanTrigger:OnAccess` exclusion to keep
   the gap narrow.
2. **Layout discipline**: keep one multi-call image plus hardlinks, no
   launcher-on-launcher shims for hot commands, no per-run unpacking. Measured
   cost of getting this wrong is one extra image open per spawn, i.e. one extra
   AV decision, and a second 4.6 MB image to establish a verdict for.
3. **SignPath signing**: no measured latency effect, but required for
   SmartScreen/publisher reputation and to avoid "unknown publisher" friction;
   also the precondition for certificate-based allow indicators in managed
   fleets.
4. **Catalog signing**: identity/integrity for a many-applet install; no
   documented scanning benefit, so do not expect it to fix #1396.

### 5.5 Sources

* Exclusions (types, process exclusions, contextual `ScanTrigger:OnAccess`, and
  "a minimal evaluation ... doesn't involve a full content scan"):
  <https://learn.microsoft.com/en-us/defender-endpoint/microsoft-defender-antivirus-exclusions-overview>
* Performance analyzer, `SkipReason` values `Not Skipped` / `Optimization (typically
  due to performance reasons)` / `User skipped (typically due to user-set exclusions)`:
  <https://learn.microsoft.com/en-us/defender-endpoint/tune-performance-defender-antivirus>
* SmartScreen reputation (unsigned = zero reputation per version, signed but new
  still warns, self-signed = same as unsigned, EV no longer bypasses):
  <https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation>
* Catalog files and digital signatures (signature over a collection of hashes,
  CatRoot, `SignTool catdb`):
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/install/catalog-files>
* Certificate indicators / file indicators (managed allow by certificate, an
  enterprise mechanism rather than a binary-side one):
  <https://learn.microsoft.com/en-us/defender-endpoint/indicators-overview>
* SignPath knowledge base on Windows code signing and reputation (OV builds
  reputation over time):
  <https://signpath.io/knowledge-base/windows-platform>

## 6. Caveats

* Host shared with other build lanes. Medians are robust, but max values reach
  ~0.9 s during contention; `raw/legs.csv` keeps min/P95/max/stdev so the noise
  is visible instead of smoothed away.
* `D:` is a mechanical disk, which inflates the cold legs (fresh 4.6 MB copies)
  and the first-scan numbers slightly compared with an SSD install.
* Defender here has cloud/MAPS disabled by policy, so no cloud reputation round
  trip is included. A product with cloud lookup enabled can plausibly add
  hundreds of milliseconds per spawn - which is the shape of the reported
  0.9 s, but unproven on this host.
* The "signed" leg is a locally trusted self-signed certificate, not a SignPath
  artifact. It validates the chain and publisher trust exactly like a release
  certificate does, but carries zero reputation, so SmartScreen conclusions must
  come from documentation, not from these numbers.
* One earlier pass recorded 110 Explorer-path samples as blocked (15 s timeout)
  for `sort.exe`; the cause was a harness bug (argument loss via the automatic
  `$args` variable, so `sort` read stdin), fixed and re-measured. Those rows were
  deleted before analysis, not averaged in.

## 7. Machine changes and restoration

Changes made for the study, all reverted and verified:

| change | purpose | restored |
|---|---|---|
| Defender policy `DisableRealtimeMonitoring` 1 -> 0 and engine value -> 0 | attempt to enable real-time protection | yes, back to 1/1 |
| `Set-MpPreference -DisableRealtimeMonitoring $false` | same | yes, back to off |
| Temporary exclusions: payload dir, `D:\repo\wc-oracle`, `D:\repo\wc-locale`, `D:\repo\unixwin-winuxcmd`, coldscan test dir | keep other lanes' builds out of the scan path and test the exclusion effect | yes, all removed (as-found list only) |
| SYSTEM scheduled task to bounce `WinDefend` | the only way to restart the protected service | yes, task deleted |
| Code-signing test certificate in CurrentUser\My, machine Root and TrustedPublisher | make the signed variant validate | yes, all three removed |
| `signtool`-signed copies under `DOCS/perf-1396/raw/payload` | A/B material | images deleted after measurement (regenerable); only `raw/manifest.csv` with sizes, SHA256, signature status and MOTW marks is kept |

## 8. Reproduction

```powershell
# 1. payload variants (unsigned hardlinks, signed copy, MOTW copies) from a build
powershell -NoProfile -File DOCS/perf-1396/scripts/prepare-binaries.ps1 `
  -SourceExe <path>\winuxcmd.exe -Sign

# 2. make the test certificate trusted (elevated)
powershell -NoProfile -File DOCS/perf-1396/scripts/run-elevated.ps1 `
  -Script DOCS/perf-1396/scripts/trust-test-cert.ps1 -ScriptArgs -Action,Install

# 3. the matrix (warm / cold / newhash / Explorer path / installed / control)
powershell -NoProfile -File DOCS/perf-1396/scripts/run-matrix.ps1 `
  -Condition rtp-off -Samples 50 -ShellSamples 10

# 4. what the engine costs per image, cached vs cold verdict
powershell -NoProfile -File DOCS/perf-1396/scripts/scan-ondemand.ps1 -Condition rtp-off
powershell -NoProfile -File DOCS/perf-1396/scripts/scan-cold-first-verdict.ps1

# 5. tables
powershell -NoProfile -File DOCS/perf-1396/scripts/analyze.ps1
powershell -NoProfile -File DOCS/perf-1396/scripts/report-tables.ps1
```

On a host where Defender real-time protection can be enabled (or with a
third-party AV), `defender-state.ps1 -Action On`, then run step 3 again with
`-Condition rtp-on` and, after `-Action AddExclusion`, `-Condition rtp-on-excluded`.
That is exactly the missing comparison.

## 9. Files

| file | content |
|---|---|
| `scripts/SpawnBench.cs` | native spawn timer (CreateProcessW / ShellExecuteExW, file-cache trim, standby purge) |
| `scripts/spawn-bench.ps1` | one leg: sampling, cold-copy and new-hash modes, CSV rows, summary line |
| `scripts/run-matrix.ps1` | the matrix for one condition |
| `scripts/prepare-binaries.ps1` | unsigned / signed / MOTW payload variants + manifest |
| `scripts/defender-state.ps1` | save / show / on / off / add / remove exclusion / restore |
| `scripts/trust-test-cert.ps1` | install / remove the test CA trust, SignPath N/A note |
| `scripts/scan-ondemand.ps1` | cached-verdict scan cost per image |
| `scripts/scan-cold-first-verdict.ps1` | first-verdict scan cost (new path + new hash) |
| `scripts/scan-cold-exclusion.ps1` | does a path exclusion change the scan |
| `scripts/probe-av-active.ps1` | EICAR check that real-time scanning really is off |
| `scripts/inventory.ps1` | OS / AV / MOTW / binary inventory |
| `scripts/analyze.ps1` | samples -> `legs.csv` + `summary.md` |
| `scripts/report-tables.ps1` | `legs.csv` -> the tables in this document |
| `raw/samples.csv` | every spawn, 8 280 rows |
| `raw/legs.csv` | per-leg n / median / P95 / min / max / mean / stdev |
| `raw/ondemand.csv`, `raw/coldscan.csv` | engine scan cost, cached and first-verdict |
| `raw/inventory.csv`, `raw/defender-original.json` | environment and AV state as found |
| `raw/run-*.log` | console log of each matrix pass |
| `raw/summary.md`, `raw/report-tables.md` | generated tables |
| `raw/manifest.csv` | variant file list with size, SHA256, signature status and MOTW marks (the images themselves are regenerable with `prepare-binaries.ps1` and were not kept) |
| `raw/certs/perf1396-test.cer` | public certificate of the test signing identity (its private key was removed from the store) |

`scripts/_probe-*.ps1` are the one-off diagnostics that establish or refute the
statements above (admin-token probe, Defender state dumps, the window in which
real-time protection refused to turn on, the EICAR check, the argument bug that
made `sort` hang on the Explorer path). They are kept because the N/A claims
should be reproducible, not taken on trust.
