
## Condition: rtp-off

#### Warm, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| installed | git | 50 | 12.49 | 15.25 | 9.98 | 17.57 |
| installed | ls | 50 | 16.86 | 20.48 | 12.79 | 24.36 |
| installed | sort | 50 | 16.34 | 18.91 | 12.74 | 22.01 |
| installed | which | 50 | 16.45 | 19 | 13.14 | 23.91 |
| motw-signed | git | 50 | 12.54 | 16.85 | 10.36 | 18.34 |
| motw-signed | ls | 50 | 49.69 | 516.93 | 13.19 | 626.36 |
| motw-signed | sort | 50 | 13.67 | 15.92 | 12.53 | 165.8 |
| motw-signed | which | 50 | 14.27 | 15.2 | 12.92 | 16.07 |
| motw-unsigned | git | 50 | 12.19 | 18.7 | 10.43 | 19.3 |
| motw-unsigned | ls | 50 | 13.43 | 15.84 | 12.42 | 16.33 |
| motw-unsigned | sort | 50 | 14.21 | 16.8 | 12.87 | 18.54 |
| motw-unsigned | which | 50 | 14.11 | 15.65 | 12.82 | 15.72 |
| signed | git | 50 | 11.04 | 21.27 | 10.07 | 68.84 |
| signed | ls | 50 | 16.82 | 19.51 | 14.02 | 20.9 |
| signed | sort | 50 | 14.1 | 15.84 | 12.79 | 18.21 |
| signed | which | 50 | 30.91 | 108.9 | 13.62 | 115.49 |
| unsigned | git | 50 | 14.14 | 16.1 | 11.6 | 16.5 |
| unsigned | ls | 50 | 17.55 | 20.76 | 14.59 | 21.26 |
| unsigned | sort | 50 | 14.22 | 21.49 | 12.69 | 27.56 |
| unsigned | which | 50 | 14.18 | 18.46 | 12.92 | 18.7 |

#### Cold: fresh copy at a new path, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 15.98 | 38.06 | 13.39 | 49.09 |
| motw-signed | ls | 50 | 18.87 | 51.19 | 16.87 | 85.54 |
| motw-signed | sort | 50 | 18.22 | 29.32 | 16.36 | 34.08 |
| motw-signed | which | 50 | 17.58 | 25.08 | 16.04 | 27.82 |
| motw-unsigned | git | 50 | 14.8 | 23.56 | 13.08 | 378.8 |
| motw-unsigned | ls | 50 | 148.21 | 454.05 | 16.27 | 908.78 |
| motw-unsigned | sort | 50 | 19.11 | 36.05 | 16.39 | 374.55 |
| motw-unsigned | which | 50 | 18.55 | 39.24 | 16.73 | 230.51 |
| signed | git | 50 | 14.18 | 16.34 | 12.98 | 50.65 |
| signed | ls | 50 | 22.92 | 57 | 17.52 | 238.52 |
| signed | sort | 50 | 17.62 | 27.77 | 15.94 | 36.56 |
| signed | which | 50 | 18.83 | 52.54 | 15.83 | 57.23 |
| unsigned | git | 50 | 14.07 | 53.61 | 12.88 | 271.47 |
| unsigned | ls | 50 | 24.58 | 124.97 | 18.01 | 153.06 |
| unsigned | sort | 50 | 22.41 | 54.61 | 17.63 | 176.8 |
| unsigned | which | 50 | 17.48 | 271.58 | 16.05 | 372.39 |

#### Cold + new content hash, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 22.19 | 38.43 | 17.29 | 41.35 |
| motw-signed | ls | 50 | 20.01 | 42.53 | 18.15 | 46.86 |
| motw-signed | sort | 50 | 29.63 | 44.26 | 19.67 | 50.34 |
| motw-signed | which | 50 | 19.93 | 23.79 | 16.61 | 30.94 |
| motw-unsigned | git | 50 | 41.9 | 86.38 | 18.42 | 116.21 |
| motw-unsigned | ls | 50 | 24.07 | 366.86 | 18.26 | 720.27 |
| motw-unsigned | sort | 50 | 28.63 | 41.26 | 19.18 | 48 |
| motw-unsigned | which | 50 | 20.71 | 34.28 | 18.6 | 40.35 |
| signed | git | 50 | 16.55 | 22.81 | 15.1 | 24.15 |
| signed | ls | 50 | 22.29 | 36.45 | 18.35 | 78.01 |
| signed | sort | 50 | 19.91 | 23.67 | 18.18 | 40.15 |
| signed | which | 50 | 24.01 | 37.85 | 16.44 | 47.54 |
| unsigned | git | 50 | 17.42 | 24.23 | 15.83 | 35.32 |
| unsigned | ls | 50 | 26.91 | 41.32 | 21.78 | 44.12 |
| unsigned | sort | 50 | 24.31 | 38.01 | 17.53 | 40.5 |
| unsigned | which | 50 | 20.54 | 40.03 | 18.36 | 46.02 |

#### Warm, ShellExecuteEx (Explorer path, ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 10 | 80.4 | 106.84 | 64.6 | 106.84 |
| motw-signed | ls | 10 | 70.71 | 77.75 | 59.16 | 77.75 |
| motw-signed | sort | 10 | 84.9 | 101.88 | 73.7 | 101.88 |
| motw-signed | which | 10 | 68.9 | 83.31 | 62.76 | 83.31 |
| motw-unsigned | git | 10 | 74.64 | 84.79 | 68.61 | 84.79 |
| motw-unsigned | ls | 10 | 73.33 | 89.91 | 61.44 | 89.91 |
| motw-unsigned | sort | 10 | 77.74 | 83.22 | 72.45 | 83.22 |
| motw-unsigned | which | 10 | 72.18 | 84.77 | 66.42 | 84.77 |
| signed | git | 10 | 58.74 | 69.03 | 53.75 | 69.03 |
| signed | ls | 10 | 113.84 | 348.08 | 63.67 | 348.08 |
| signed | sort | 10 | 65.67 | 75.27 | 59.62 | 75.27 |
| signed | which | 10 | 64.62 | 70.02 | 62.17 | 70.02 |
| unsigned | git | 10 | 62.68 | 78.28 | 55.25 | 78.28 |
| unsigned | ls | 10 | 76.79 | 84.97 | 57.26 | 84.97 |
| unsigned | sort | 10 | 74.94 | 81.67 | 69.23 | 81.67 |
| unsigned | which | 10 | 63.08 | 74.17 | 58.38 | 74.17 |

## Condition: rtp-off-quiet

#### Warm, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| installed | git | 50 | 12.8 | 16.35 | 10.2 | 18.11 |
| installed | ls | 50 | 16.54 | 19.14 | 12.76 | 69.62 |
| installed | sort | 50 | 15.76 | 17.82 | 13.24 | 19.8 |
| installed | which | 50 | 15.4 | 18.41 | 13.09 | 19.26 |
| motw-signed | git | 50 | 11.69 | 14.72 | 10.31 | 14.93 |
| motw-signed | ls | 50 | 14.91 | 18.83 | 13.11 | 22.01 |
| motw-signed | sort | 50 | 14.79 | 18.22 | 13.22 | 19.71 |
| motw-signed | which | 50 | 15.01 | 18 | 12.5 | 20.56 |
| motw-unsigned | git | 50 | 11.44 | 15.7 | 9.92 | 18.15 |
| motw-unsigned | ls | 50 | 14.65 | 17.72 | 12.68 | 18.83 |
| motw-unsigned | sort | 50 | 14.83 | 17.48 | 13.13 | 17.78 |
| motw-unsigned | which | 50 | 16.14 | 18.64 | 13.09 | 19.43 |
| signed | git | 50 | 12.11 | 16 | 9.95 | 24.46 |
| signed | ls | 50 | 20.73 | 30.72 | 15.47 | 31.86 |
| signed | sort | 50 | 15.02 | 17.47 | 13.08 | 23.79 |
| signed | which | 50 | 16.13 | 21.04 | 13.44 | 29.91 |
| unsigned | git | 50 | 13.55 | 15.87 | 10.84 | 18.91 |
| unsigned | ls | 50 | 17.35 | 20.04 | 13.78 | 21.31 |
| unsigned | sort | 50 | 18.44 | 26.89 | 14.11 | 29.12 |
| unsigned | which | 50 | 17.91 | 22.43 | 13.92 | 26.14 |

#### Cold: fresh copy at a new path, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 15.33 | 18.62 | 13.82 | 405.92 |
| motw-signed | ls | 50 | 22.73 | 42.66 | 16.95 | 211.66 |
| motw-signed | sort | 50 | 18.93 | 21.71 | 17.02 | 220.08 |
| motw-signed | which | 50 | 19.67 | 24.11 | 17.11 | 24.85 |
| motw-unsigned | git | 50 | 15.37 | 49.07 | 13.34 | 361.6 |
| motw-unsigned | ls | 50 | 18.55 | 22.74 | 16.05 | 211.28 |
| motw-unsigned | sort | 50 | 18.55 | 21.49 | 16.19 | 143.25 |
| motw-unsigned | which | 50 | 19.49 | 258.41 | 16.42 | 662.73 |
| signed | git | 50 | 15.22 | 18.4 | 12.87 | 162.91 |
| signed | ls | 50 | 25.66 | 62.38 | 21.49 | 124.43 |
| signed | sort | 50 | 18.8 | 27.08 | 15.9 | 32.34 |
| signed | which | 50 | 20.47 | 31.67 | 16.81 | 36.2 |
| unsigned | git | 50 | 17.52 | 32.34 | 14.16 | 44.05 |
| unsigned | ls | 50 | 22.59 | 29.66 | 18.1 | 35.54 |
| unsigned | sort | 50 | 23.31 | 198.56 | 17.96 | 398.09 |
| unsigned | which | 50 | 23.48 | 33.55 | 18.17 | 207.68 |

#### Cold + new content hash, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 17.78 | 22.58 | 14.85 | 77.19 |
| motw-signed | ls | 50 | 22.79 | 39.84 | 19.73 | 40.33 |
| motw-signed | sort | 50 | 21.87 | 25.26 | 19.69 | 26.88 |
| motw-signed | which | 50 | 21.97 | 38.77 | 19.16 | 42.41 |
| motw-unsigned | git | 50 | 19.84 | 39.55 | 15.93 | 52.78 |
| motw-unsigned | ls | 50 | 21.49 | 40.99 | 18.29 | 49.15 |
| motw-unsigned | sort | 50 | 21.11 | 29.95 | 19.2 | 66.4 |
| motw-unsigned | which | 50 | 21.02 | 23.39 | 19.11 | 25.45 |
| signed | git | 50 | 17.64 | 35.8 | 13.88 | 36.61 |
| signed | ls | 50 | 26.18 | 50.45 | 19.42 | 83.6 |
| signed | sort | 50 | 21.1 | 38.76 | 18.71 | 50.26 |
| signed | which | 50 | 27.72 | 46.12 | 19.05 | 57.7 |
| unsigned | git | 50 | 22.44 | 40.86 | 15.83 | 49.33 |
| unsigned | ls | 50 | 31.58 | 49.68 | 21.2 | 83.29 |
| unsigned | sort | 50 | 24.94 | 36.29 | 20.39 | 39.86 |
| unsigned | which | 50 | 23.21 | 33.26 | 16.73 | 37.67 |

#### Warm, ShellExecuteEx (Explorer path, ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 10 | 78.94 | 95.25 | 58.51 | 95.25 |
| motw-signed | ls | 10 | 81.01 | 104.5 | 72.6 | 104.5 |
| motw-signed | sort | 10 | 80.52 | 245.27 | 72.03 | 245.27 |
| motw-signed | which | 10 | 87.38 | 104.23 | 70.29 | 104.23 |
| motw-unsigned | git | 10 | 85.35 | 128.19 | 61.26 | 128.19 |
| motw-unsigned | ls | 10 | 79.51 | 88.69 | 74.15 | 88.69 |
| motw-unsigned | sort | 10 | 78.13 | 98.63 | 57.73 | 98.63 |
| motw-unsigned | which | 10 | 81.17 | 89.13 | 73.28 | 89.13 |
| signed | git | 10 | 75.35 | 90.04 | 66.1 | 90.04 |
| signed | ls | 10 | 75.21 | 77.83 | 70.08 | 77.83 |
| signed | sort | 10 | 78.7 | 98.07 | 70.71 | 98.07 |
| signed | which | 10 | 71.42 | 88.21 | 60.19 | 88.21 |
| unsigned | git | 10 | 78.3 | 81.56 | 67.88 | 81.56 |
| unsigned | ls | 10 | 95.38 | 107.15 | 84.22 | 107.15 |
| unsigned | sort | 10 | 82.52 | 88.75 | 72.63 | 88.75 |
| unsigned | which | 10 | 90.89 | 125.2 | 75.77 | 125.2 |

## Condition: rtp-off-rerun

#### Warm, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| installed | git | 50 | 12.66 | 16.28 | 10.22 | 18.08 |
| installed | ls | 50 | 16.67 | 21.62 | 13.63 | 22.73 |
| installed | sort | 50 | 15.82 | 19.88 | 12.93 | 21.96 |
| installed | which | 50 | 14.87 | 18.73 | 12.57 | 21.93 |
| motw-signed | git | 50 | 14.35 | 17.33 | 10.85 | 18.97 |
| motw-signed | ls | 50 | 16.54 | 20.28 | 13.3 | 25.36 |
| motw-signed | sort | 50 | 15.26 | 17.32 | 13.05 | 18.51 |
| motw-signed | which | 50 | 17.46 | 21.07 | 13.96 | 21.89 |
| motw-unsigned | git | 50 | 13.25 | 18.54 | 10.73 | 20.36 |
| motw-unsigned | ls | 50 | 17.53 | 21.79 | 14.14 | 22.19 |
| motw-unsigned | sort | 50 | 17.72 | 21.63 | 15.17 | 30.82 |
| motw-unsigned | which | 50 | 17.75 | 22.56 | 13.41 | 32.89 |
| signed | git | 50 | 14.53 | 21.76 | 11.13 | 23.04 |
| signed | ls | 50 | 20.81 | 25.6 | 15.21 | 34.86 |
| signed | sort | 50 | 16.41 | 18.84 | 13.35 | 20.02 |
| signed | which | 50 | 17.79 | 19.27 | 14.08 | 19.55 |
| unsigned | git | 50 | 11.17 | 16.27 | 10.25 | 17.25 |
| unsigned | ls | 50 | 16.64 | 18.94 | 13.65 | 19.08 |
| unsigned | sort | 50 | 14.26 | 17.58 | 13.12 | 27.55 |
| unsigned | which | 50 | 17.59 | 21.53 | 14.42 | 24.72 |

#### Cold: fresh copy at a new path, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 16.7 | 37.87 | 13.89 | 194.01 |
| motw-signed | ls | 50 | 26.96 | 44.23 | 17.96 | 221.28 |
| motw-signed | sort | 50 | 22.6 | 35.8 | 17.56 | 133.68 |
| motw-signed | which | 50 | 20.48 | 33.79 | 17.75 | 129.85 |
| motw-unsigned | git | 50 | 18.01 | 30.98 | 14.25 | 41.7 |
| motw-unsigned | ls | 50 | 28.34 | 50.67 | 19.66 | 52.44 |
| motw-unsigned | sort | 50 | 28.24 | 39.55 | 20.4 | 44.56 |
| motw-unsigned | which | 50 | 30.27 | 48.32 | 19.82 | 65.37 |
| signed | git | 50 | 20.5 | 34.93 | 15.25 | 296.48 |
| signed | ls | 50 | 35.51 | 50.15 | 18.19 | 51.79 |
| signed | sort | 50 | 28.92 | 42.81 | 17.6 | 285 |
| signed | which | 50 | 28.09 | 50.35 | 19.18 | 65.59 |
| unsigned | git | 50 | 15.67 | 342.39 | 13.13 | 382.94 |
| unsigned | ls | 50 | 21.8 | 25.03 | 17.74 | 30.63 |
| unsigned | sort | 50 | 19 | 29.84 | 16.65 | 38.28 |
| unsigned | which | 50 | 22.71 | 26.28 | 18.7 | 28.08 |

#### Cold + new content hash, CreateProcessW (ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 50 | 20.66 | 29.45 | 17.17 | 37.6 |
| motw-signed | ls | 50 | 28.63 | 52.5 | 21.04 | 135.86 |
| motw-signed | sort | 50 | 23.1 | 40.13 | 19.58 | 41.13 |
| motw-signed | which | 50 | 21.92 | 36.89 | 18.43 | 43.9 |
| motw-unsigned | git | 50 | 22.54 | 42.93 | 18.56 | 109.72 |
| motw-unsigned | ls | 50 | 35.29 | 43.39 | 17.47 | 51.17 |
| motw-unsigned | sort | 50 | 26.09 | 43.77 | 21.23 | 144.5 |
| motw-unsigned | which | 50 | 32.06 | 50.7 | 20.92 | 108.84 |
| signed | git | 50 | 25.11 | 42.3 | 17.42 | 44.27 |
| signed | ls | 50 | 24.12 | 48.69 | 18.92 | 190.46 |
| signed | sort | 50 | 29.85 | 44.52 | 20.57 | 48.09 |
| signed | which | 50 | 31.58 | 90.01 | 21.99 | 219.96 |
| unsigned | git | 50 | 24.87 | 42.98 | 14.57 | 45.82 |
| unsigned | ls | 50 | 24.45 | 28.27 | 20.41 | 75.6 |
| unsigned | sort | 50 | 19.06 | 33.87 | 16.36 | 38.63 |
| unsigned | which | 50 | 24.86 | 37.74 | 18.26 | 51.51 |

#### Warm, ShellExecuteEx (Explorer path, ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 10 | 73.82 | 104.09 | 68.82 | 104.09 |
| motw-signed | ls | 10 | 89.2 | 128.99 | 80.79 | 128.99 |
| motw-signed | sort | 10 | 90.84 | 117.58 | 68.52 | 117.58 |
| motw-signed | which | 10 | 80.68 | 105.13 | 68.1 | 105.13 |
| motw-unsigned | git | 10 | 79.82 | 98.08 | 77.78 | 98.08 |
| motw-unsigned | ls | 10 | 88.38 | 99.51 | 82.16 | 99.51 |
| motw-unsigned | sort | 10 | 90.48 | 114.51 | 85.15 | 114.51 |
| motw-unsigned | which | 10 | 177.77 | 262.16 | 86.67 | 262.16 |
| signed | git | 10 | 84.11 | 159.01 | 79.09 | 159.01 |
| signed | ls | 10 | 88.4 | 99.07 | 77.03 | 99.07 |
| signed | sort | 10 | 96.21 | 114.05 | 85.28 | 114.05 |
| signed | which | 10 | 91.66 | 105.33 | 83.44 | 105.33 |
| unsigned | git | 10 | 92.87 | 109.33 | 78.01 | 109.33 |
| unsigned | ls | 10 | 90.68 | 99.68 | 77.36 | 99.68 |
| unsigned | sort | 10 | 73.95 | 80.54 | 63.27 | 80.54 |
| unsigned | which | 10 | 91.02 | 103.26 | 72.06 | 103.26 |

## Deltas

| condition | binary | unsigned warm | signed warm | signed - unsigned | cold (copy) | cold - warm | newhash | newhash - warm |
|---|---|---|---|---|---|---|---|---|
| rtp-off | ls | 17.55 | 16.82 | -0.73 | 24.58 | 7.03 | 26.91 | 9.36 |
| rtp-off | which | 14.18 | 30.91 | 16.73 | 17.48 | 3.3 | 20.54 | 6.36 |
| rtp-off | sort | 14.22 | 14.1 | -0.12 | 22.41 | 8.19 | 24.31 | 10.09 |
| rtp-off | git | 14.14 | 11.04 | -3.1 | 14.07 | -0.07 | 17.42 | 3.28 |
| rtp-off-quiet | ls | 17.35 | 20.73 | 3.38 | 22.59 | 5.24 | 31.58 | 14.23 |
| rtp-off-quiet | which | 17.91 | 16.13 | -1.78 | 23.48 | 5.57 | 23.21 | 5.3 |
| rtp-off-quiet | sort | 18.44 | 15.02 | -3.42 | 23.31 | 4.87 | 24.94 | 6.5 |
| rtp-off-quiet | git | 13.55 | 12.11 | -1.44 | 17.52 | 3.97 | 22.44 | 8.89 |
| rtp-off-rerun | ls | 16.64 | 20.81 | 4.17 | 21.8 | 5.16 | 24.45 | 7.81 |
| rtp-off-rerun | which | 17.59 | 17.79 | 0.2 | 22.71 | 5.12 | 24.86 | 7.27 |
| rtp-off-rerun | sort | 14.26 | 16.41 | 2.15 | 19 | 4.74 | 19.06 | 4.8 |
| rtp-off-rerun | git | 11.17 | 14.53 | 3.36 | 15.67 | 4.5 | 24.87 | 13.7 |

#### On-demand MpCmdRun scan, cached verdict (rtp-off, ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 5 | 25.6 | 26.3 | 23.5 | 26.3 |
| motw-signed | ls | 5 | 26.5 | 27.4 | 25.6 | 27.4 |
| motw-unsigned | git | 5 | 27.8 | 30.2 | 24.5 | 30.2 |
| motw-unsigned | ls | 5 | 25.3 | 38.7 | 22.8 | 38.7 |
| signed | git | 5 | 24.8 | 35.7 | 21.3 | 35.7 |
| signed | ls | 5 | 28 | 49.9 | 26.5 | 49.9 |
| unsigned | git | 5 | 25 | 28.5 | 22.3 | 28.5 |
| unsigned | ls | 5 | 25.5 | 130 | 22.3 | 130 |

#### On-demand MpCmdRun scan, cached verdict (rtp-off-excluded, ms)

| variant | binary | n | median_ms | p95_ms | min_ms | max_ms |
|---|---|---|---|---|---|---|
| motw-signed | git | 5 | 25 | 28.2 | 23.3 | 28.2 |
| motw-signed | ls | 5 | 25.3 | 27.4 | 21.7 | 27.4 |
| motw-unsigned | git | 5 | 25.2 | 32.3 | 24.8 | 32.3 |
| motw-unsigned | ls | 5 | 29.9 | 30.6 | 27.6 | 30.6 |
| signed | git | 5 | 27 | 31.1 | 26.5 | 31.1 |
| signed | ls | 5 | 27.5 | 39.9 | 23.8 | 39.9 |
| unsigned | git | 5 | 24.6 | 27.6 | 22.2 | 27.6 |
| unsigned | ls | 5 | 26.7 | 81.7 | 24.7 | 81.7 |
