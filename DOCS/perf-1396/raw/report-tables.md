### Warm spawn, CreateProcessW: median / P95 (ms)

| variant | applet | run 1 median | run 1 P95 | run 2 median | run 2 P95 | notes |
|---|---|---|---|---|---|---|
| installed | ls | 16.5 | 19.1 | 16.9 | 20.5 | on PATH, inside the existing Defender exclusions |
| installed | which | 15.4 | 18.4 | 16.5 | 19.0 | on PATH, inside the existing Defender exclusions |
| installed | sort | 15.8 | 17.8 | 16.3 | 18.9 | on PATH, inside the existing Defender exclusions |
| installed | git | 12.8 | 16.4 | 12.5 | 15.3 | on PATH, inside the existing Defender exclusions |
| unsigned | ls | 17.4 | 20.0 | 17.6 | 20.8 | local build, no signature |
| unsigned | which | 17.9 | 22.4 | 14.2 | 18.5 | local build, no signature * |
| unsigned | sort | 18.4 | 26.9 | 14.2 | 21.5 | local build, no signature * |
| unsigned | git | 13.6 | 15.9 | 14.1 | 16.1 | local build, no signature |
| signed | ls | 20.7 | 30.7 | 16.8 | 19.5 | local build + trusted test cert |
| signed | which | 16.1 | 21.0 | 30.9 | 108.9 | local build + trusted test cert * |
| signed | sort | 15.0 | 17.5 | 14.1 | 15.8 | local build + trusted test cert |
| signed | git | 12.1 | 16.0 | 11.0 | 21.3 | local build + trusted test cert |
| motw-unsigned | ls | 14.7 | 17.7 | 13.4 | 15.8 | unsigned + Zone.Identifier |
| motw-unsigned | which | 16.1 | 18.6 | 14.1 | 15.7 | unsigned + Zone.Identifier |
| motw-unsigned | sort | 14.8 | 17.5 | 14.2 | 16.8 | unsigned + Zone.Identifier |
| motw-unsigned | git | 11.4 | 15.7 | 12.2 | 18.7 | unsigned + Zone.Identifier |
| motw-signed | ls | 14.9 | 18.8 | 49.7 | 516.9 | signed + Zone.Identifier * |
| motw-signed | which | 15.0 | 18.0 | 14.3 | 15.2 | signed + Zone.Identifier |
| motw-signed | sort | 14.8 | 18.2 | 13.7 | 15.9 | signed + Zone.Identifier |
| motw-signed | git | 11.7 | 14.7 | 12.5 | 16.9 | signed + Zone.Identifier |

### Cold (fresh byte copy at a new path) and new content hash: median (ms)

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

### Explorer path (ShellExecuteEx), warm, 10 samples per leg: median / P95 (ms)

| variant | applet | median | P95 | blocked |
|---|---|---|---|---|
| unsigned | ls | 95.4 | 107.2 | 0 |
| unsigned | which | 90.9 | 125.2 | 0 |
| unsigned | sort | 82.5 | 88.8 | 0 |
| unsigned | git | 78.3 | 81.6 | 0 |
| signed | ls | 75.2 | 77.8 | 0 |
| signed | which | 71.4 | 88.2 | 0 |
| signed | sort | 78.7 | 98.1 | 0 |
| signed | git | 75.4 | 90.0 | 0 |
| motw-unsigned | ls | 79.5 | 88.7 | 0 |
| motw-unsigned | which | 81.2 | 89.1 | 0 |
| motw-unsigned | sort | 78.1 | 98.6 | 0 |
| motw-unsigned | git | 85.4 | 128.2 | 0 |
| motw-signed | ls | 81.0 | 104.5 | 0 |
| motw-signed | which | 87.4 | 104.2 | 0 |
| motw-signed | sort | 80.5 | 245.3 | 0 |
| motw-signed | git | 78.9 | 95.3 | 0 |

### Defender engine scan cost per image (MpCmdRun)

| variant | image | cached verdict, median (ms) | cached verdict, excluded path (ms) | cold verdict (new path+hash), median (ms) |
|---|---|---|---|---|
| motw-signed | git | 25.6 | 25.0 | n/a |
| motw-signed | ls | 26.5 | 25.3 | 191.5 |
| motw-unsigned | git | 27.8 | 25.2 | n/a |
| motw-unsigned | ls | 25.3 | 29.9 | 179.0 |
| signed | git | 24.8 | 27.0 | 47.7 |
| signed | ls | 28.0 | 27.5 | 170.3 |
| unsigned | git | 25.0 | 24.6 | 47.5 |
| unsigned | ls | 25.5 | 26.7 | 167.0 |
| microsoft-signed | whoami | n/a | n/a | 54.0 |
