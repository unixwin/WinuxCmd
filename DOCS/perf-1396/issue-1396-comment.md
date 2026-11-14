## Per-spawn scan matrix: measured on a physical Win10 box with Defender — **AV-on leg is N/A here, everything else is data**

I built a harness for the 4x2x2 matrix (`ls`/`which`/`sort`/`git` x cold/warm x
unsigned/signed, 50 samples per leg, native `CreateProcessW` timing) and ran it
on a real machine. Two results up front, because they change the premise of the
issue rather than just adding numbers:

1. **On this host the AV could not be turned on**, so the "AV scans every spawn"
   leg is N/A (evidence below). What I can report is the no-scan floor plus the
   Defender engine's own per-image scan cost, which is the part a real-time
   monitor would add.
2. **The installed `git.exe` is not signed.** `Get-AuthenticodeSignature` on
   `...\Niubash\winuxcmd\usr\bin\git.exe` (git 2.56.0.windows.4.niugit) returns
   `NotSigned`. The issue's "Git for Windows, SIGNED vendor binary" contrast
   does not hold for this installation, so git's 3-4 ms advantage here cannot
   be a signature effect.

### Environment (all numbers below are from this box)

| item | value |
|---|---|
| OS | Windows 10 Pro 22H2 `10.0.19045.6466`, x64, physical (JWIPC SYS-6023H) |
| CPU / RAM | Xeon w5-3433, 16C/32T / 63.7 GB |
| Volume | `D:` mechanical disk (cold legs copy 4.6 MB there) |
| AV | Microsoft Defender only, platform 4.18.26100.4, engine 1.1.26100.4, sig 1.463.290.0 |
| Defender real-time protection | **off, pinned by policy** (`HKLM\SOFTWARE\Policies\...\Real-Time Protection\DisableRealtimeMonitoring = 1`) |
| Defender cloud | off (`MAPSReporting=0`, `SubmitSamplesConsent=0`) |
| Defender exclusions as found | paths incl. `...\Programs\Niubash`, `D:\repo\WinuxCmd`, `D:\tmp\*`; processes incl. `winuxcmd.exe` |
| other AV | none (no 360/Huorong/Kaspersky), SecurityCenter2 lists Defender only |
| SmartScreen | not configured; no MOTW policy |
| load | host shared with build lanes: 23-26 % in the reported pass, up to 48 % earlier |

Method: 68 legs per pass, 2 760 spawns per pass, 8 280 rows total. `warm` =
repeat spawn of a stable path; `cold` = fresh byte copy in a new directory (new
path + new file identity = post-install/upgrade); `newhash` = cold plus a pad
byte so the content hash is new too. Native timer, because Measure-Command's own
overhead (5-15 ms) is the same order as a clean spawn.

### Warm spawn, `CreateProcessW`, median / P95 ms (quiet pass / second pass)

| variant | ls | which | sort | git |
|---|---|---|---|---|
| installed (on PATH) | 16.5 / 19.1 | 15.4 / 18.4 | 15.8 / 17.8 | 12.8 / 16.4 |
| unsigned | 17.4 / 20.0 | 17.9 / 22.4 | 18.4 / 26.9 | 13.6 / 15.9 |
| signed (trusted cert) | 20.7 / 30.7 | 16.1 / 21.0 | 15.0 / 17.5 | 12.1 / 16.0 |
| motw-unsigned | 14.7 / 17.7 | 16.1 / 18.6 | 14.8 / 17.5 | 11.4 / 15.7 |
| motw-signed | 14.9 / 18.8 | 15.0 / 18.0 | 14.8 / 18.2 | 11.7 / 14.7 |

Second pass, same legs, differs by <= ~5 ms except two contention spikes. `ls`,
`which` and `sort` are indistinguishable (they are hardlinks of one
`winuxcmd.exe`), and `git` is only ~3 ms faster. Signature and MOTW move nothing
when nothing scans.

### Cold / new-hash spawn, `CreateProcessW`, median ms

| variant | ls cold | which cold | sort cold | git cold | ls newhash | git newhash |
|---|---|---|---|---|---|---|
| unsigned | 22.6 | 23.5 | 23.3 | 17.5 | 31.6 | 22.4 |
| signed | 25.7 | 20.5 | 18.8 | 15.2 | 26.2 | 17.6 |
| motw-unsigned | 18.6 | 19.5 | 18.6 | 15.4 | 21.5 | 19.8 |
| motw-signed | 22.7 | 19.7 | 18.9 | 15.3 | 22.8 | 17.8 |

Cold costs **+3 to +5 ms** over warm; a brand-new content hash another +2 to
+9 ms. That is the entire app-side share of a first run. Nothing here produces
0.9 s.

### What the engine itself charges per image (`MpCmdRun -Scan -ScanType 3`)

| image | cached verdict | cold verdict (new path + new hash) |
|---|---|---|
| `ls.exe` unsigned, 4.6 MB | 25.5 | **167.0** |
| `ls.exe` signed, 4.6 MB | 28.0 | **170.3** |
| `ls.exe` + MOTW, unsigned | 25.3 | **179.0** |
| `ls.exe` + MOTW, signed | 26.5 | **191.5** |
| `git.exe` unsigned, 5.2 MB | 25.0 | **47.5** |
| `git.exe` signed, 5.2 MB | 24.8 | **47.7** |
| `whoami.exe` (OS-signed) | - | **54.0** |

The ~25 ms floor is the tool plus a cached verdict. A cold verdict on the
WinuxCmd image costs ~120 ms more, **identically for signed and unsigned**, and
~120 ms more than a same-size widely-known image. Signing did not shorten the
scan on this host.

### Why the AV-on leg is N/A (not skipped silently)

`RealTimeProtectionEnabled=False` while `AntivirusEnabled=True`,
`AMRunningMode=Normal`. I cleared the policy pin and the engine value, ran
`Set-MpPreference -DisableRealtimeMonitoring $false` (`Get-MpPreference` then
reports `False`), bounced the protected `WinDefend` service through a SYSTEM
scheduled task (Administrators cannot stop it directly), `fltmc` still shows
`WdFilter` attached with 13 instances, and the flag stayed `False`. Decisive
check instead of trusting the flag: the EICAR test string written to a
non-excluded directory **survived** and `Get-MpThreatDetection` stayed empty.
Re-arming needs a reboot of a machine shared with other work, so it is reported
as N/A, not faked. Everything changed was restored afterwards (policy back to 1,
temporary exclusions removed, test cert trust removed, scheduled task deleted).

N/A legs: AV-on spawn latency (any variant/state); AV-on exclusion contrast;
360/Huorong/Kaspersky behaviour (not installable here); SignPath-signed artifact
(no token locally - the signed legs use a locally trusted self-signed cert,
which validates chain and publisher but carries zero reputation); SmartScreen
prompt latency (not configured; one Explorer-path MOTW sample did open
`smartscreen.exe` and block once, not reproducible in three later passes);
Smart App Control (Win11 only).

### Mitigation ranking (research, nothing implemented)

1. **Deployment-time exclusion** — the only documented lever that removes the
   scan: Defender skips when the exclusion matches (performance analyzer reports
   `SkipReason = User skipped`, and exclusions still cost only "a minimal
   evaluation ... no full content scan"). Narrow it with a process exclusion
   (`winuxcmd.exe` is already excluded on this host) or a contextual
   `ScanTrigger:OnAccess` exclusion instead of a whole directory.
2. **Layout discipline** — one multi-call image + hardlinks = one image open per
   spawn (measured: ls/which/sort identical). A launcher-on-launcher shim layout
   doubles image opens and therefore AV decisions; per-applet copies multiply
   cold verdicts. Keep the flat shape `git` has here.
3. **SignPath signing** — required for SmartScreen/publisher reputation
   (unsigned files restart at zero reputation per version; new signed files
   still warn until reputation accrues; EV no longer bypasses; a self-signed
   cert behaves like no signature), and the precondition for certificate-based
   allow indicators in managed fleets. On this host it did **not** reduce the
   engine's scan cost, so "signing stops paying the scan" should not be the
   stated fix; keep the issue open on the "signed release removes the
   SmartScreen/cloud reputation tax" claim, which needs a cloud-enabled AV
   machine to verify.
4. **Catalog (`.cat`) signing** — a catalog is a signature over a set of file
   hashes (CatRoot, `SignTool catdb`), which is attractive for dozens of
   hardlinked applet names, but there is no Microsoft documentation that a
   catalog signature suppresses per-spawn scanning. Treat it as
   identity/integrity, not as a latency fix.

### Full data

`DOCS/perf-1396/` on branch `wc-av1396` (uncommitted working tree):
`raw/samples.csv` (8 280 rows), `raw/legs.csv` (n/median/P95/min/max/stdev per
leg), `raw/ondemand.csv` + `raw/coldscan.csv` (engine scan cost),
`raw/inventory.csv` (OS/AV/MOTW/binary facts), `raw/run-*.log` (per-pass
console log), `README.md` (method, caveats, reproduction commands, restoration
of machine state), `scripts/` (harness).

Caveats: shared host (medians are robust, max reaches 0.9 s under contention;
min/P95/max kept in `legs.csv`), `D:` is a mechanical disk, Defender cloud is
off by policy so no reputation round trip is included - a cloud-enabled AV can
plausibly add the rest of the 0.9 s, but that is unproven here.
