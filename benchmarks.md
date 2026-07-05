# Benchmarks - DOG WITCH

## Checkpoint v1 (6/14/26)

### Profile

Tag: https://github.com/leegao/bcn_layer/tree/shader-v1

```
Work registers: 32 (100% used at 100% occupancy)
Uniform registers: 32 (25% used)
Shared storage size: 0 bytes
Stack use: 304 bytes
 - Alloca region: 292 bytes
16-bit arithmetic: 12%
 - Idle SIMD lanes: 26%

                                A     FMA     CVT     SFU      LS       T    Bound
Total instruction cycles:   13.31    0.74   12.81    2.00   32.20    0.00       LS
Shortest path cycles:        0.09    0.00    0.09    0.00    0.00    0.00   A, CVT
Longest path cycles:          N/A     N/A     N/A     N/A     N/A     N/A      N/A

A = Arithmetic, FMA = Arith FMA, CVT = Arith CVT, SFU = Arith SFU,
LS = Load/Store, T = Texture
```

See https://github.com/leegao/etc2_encode/actions/runs/27486249546

Dynamic Profile

```
[info]: --- Profile: encode_astc ---
[info]:   Compute Purity:       100.00%
[info]:   ALU FMA Pipe Util:    1.77%
[info]:   ALU CVT Pipe Util:    32.26%
[info]:   ALU SFU Pipe Util:    3.25%
[info]:   Load/Store Unit Util: 58.79%
[info]:   Compute Queue Cycles: 5281856 cycles (129601 warps)
[info]:   GPU Active Cycles:    5281856 cycles (129601 warps)
[info]:   --- Shader Stats: encode_astc ---
[info]:     >32 Registers:              0.00% (0 warps)
[info]:     Full Warp Grid Execution:   100.00% (129601 warps)
[info]:     Warp Branch Divergence:     0.00%
[info]:     16-bit Math Density:        5.81%
[info]:     Instruction Cache Misses:   0
[info]:     ALU Engine Starvation Rate: 91.20%
[info]:   --- L2 Cache: encode_astc ---
[info]:     L2 Cache Read Miss Rate:    46.23%
[info]:     L2 Cache Read Locality:     61.71%
[info]:     Internal Read Stall:        3.30% (346222 cycles)
[info]:   --- External Reads Profile: encode_astc ---
[info]:     Read Transactions (Packets): 1086066
[info]:     Read Beats (Bus Bursts):    4343471
[info]:     Read Payload Data Vol:      69495536 bytes
[info]:     Avg Burst Length:           16.00 bytes/tx
[info]:     External Read Stall Rate:   2.20% (116198 cycles)
[info]:     Slow External Reads (16b): 18.56% (806092 beats)
[info]:   --- External Read Transaction Queue Load: encode_astc ---
[info]:     Queue Load [ 0% -  25%]:    18.53% (201266 txs)
[info]:     Queue Load [25% -  50%]:    22.70% (246501 txs)
[info]:     Queue Load [50% -  75%]:    30.28% (328887 txs)
[info]:     Queue Load [75% - 100%]:    28.49% (309412 txs)
```

Heavily LDS bound on Mali GPUs

### Quality

```
[info]:   Diagnostics (87381 blocks, 29318073 total)
[info]:      + mean_error_squared: 0.000000 (PSNR: 76.69), running: 0.000158 (PSNR: 38.01)
[info]:      + mean_quantized_error_squared: 0.000000 (PSNR: inf), running: 0.000004 (PSNR: 53.51)
[info]:      + mean_quantized_error_squared (alt): 0.000000 (PSNR: inf), running: 0.000011 (PSNR: 49.44)
```

Across 29,318,073 4x4 blocks encoded by ASTC, the MSE of the encoding runs at ~38.01 dB (PSNR), while both the high precision quantization (53.51 db) and the low precision quantization (49.44 db) are negligible relative to the encoding error.

### Performance

```
[info]: Cleaning up batch 86 with 8 buffers, 8 descriptors, and 12 tracked queries took 44 ms
[info]:   [           all] Calls: 4     | Time:  38.73 ms | Data:    3.8 MB | Throughput:   98.4 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      3450.46 ms |        442.8 MB | Throughput:  128.3 MB/s
[info]:             + BC1a (133): 791   |      1978.91 ms |        273.4 MB | Throughput:  138.2 MB/s
[info]:             +  BC3 (137): 598   |      1201.83 ms |        141.1 MB | Throughput:  117.4 MB/s
[info]:             +  BC6 (143): 48    |        10.41 ms |          0.1 MB | Throughput:   12.0 MB/s
[info]:             +  BC7 (145): 48    |       259.31 ms |         28.1 MB | Throughput:  108.4 MB/s
[info]:   [decompress_bcn] Calls: 4     | Time:  13.00 ms | Data:    3.8 MB | Throughput:  293.2 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |       526.65 ms |        442.8 MB | Throughput:  840.7 MB/s
[info]:             + BC1a (133): 791   |       248.92 ms |        273.4 MB | Throughput: 1098.3 MB/s
[info]:             +  BC3 (137): 598   |       201.65 ms |        141.1 MB | Throughput:  699.9 MB/s
[info]:             +  BC6 (143): 48    |         1.81 ms |          0.1 MB | Throughput:   69.2 MB/s
[info]:             +  BC7 (145): 48    |        74.27 ms |         28.1 MB | Throughput:  378.4 MB/s
[info]:   [   encode_astc] Calls: 4     | Time:  24.60 ms | Data:    3.8 MB | Throughput:  155.0 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      2777.75 ms |        442.8 MB | Throughput:  159.4 MB/s
[info]:             + BC1a (133): 791   |      1655.45 ms |        273.4 MB | Throughput:  165.2 MB/s
[info]:             +  BC3 (137): 598   |       939.29 ms |        141.1 MB | Throughput:  150.3 MB/s
[info]:             +  BC6 (143): 48    |         5.67 ms |          0.1 MB | Throughput:   22.1 MB/s
[info]:             +  BC7 (145): 48    |       177.35 ms |         28.1 MB | Throughput:  158.5 MB/s
```

On the G615, BCn decompression is around 800-1000 MB/s whlie astc compression is fairly uniform at ~160 MB/s, for a total throughput of ~130 MB/s

## Checkpoint v2 (7/5/26)

### Profile

Tag: https://github.com/leegao/bcn_layer/tree/shader-v2

```
Work registers: 32 (100% used at 100% occupancy)
Uniform registers: 32 (25% used)
Shared storage size: 0 bytes
Stack use: 272 bytes
 - Alloca region: 272 bytes
16-bit arithmetic: 38%
 - Idle SIMD lanes: 29%

                                A     FMA     CVT     SFU      LS       T    Bound
Total instruction cycles:   14.80    1.59   14.00    3.19   25.10    0.00       LS
Shortest path cycles:        0.09    0.00    0.09    0.00    0.00    0.00   A, CVT
Longest path cycles:          N/A     N/A     N/A     N/A     N/A     N/A      N/A

A = Arithmetic, FMA = Arith FMA, CVT = Arith CVT, SFU = Arith SFU,
LS = Load/Store, T = Texture
```

See https://github.com/leegao/etc2_encode/actions/runs/27504985585

**Dynamic Profile**

Run with `BCN_SAMPLE_GPU_COUNTERS=1`

```
[info]: --- Profile: encode_astc ---
[info]:   Compute Purity:       100.00%
[info]:   ALU FMA Pipe Util:    1.23%
[info]:   ALU CVT Pipe Util:    24.22%
[info]:   ALU SFU Pipe Util:    4.32%
[info]:   Load/Store Unit Util: 48.17%
[info]:   Compute Queue Cycles: 6291513 cycles (129601 warps)
[info]:   GPU Active Cycles:    6291513 cycles (129601 warps)
[info]:   --- Shader Stats: encode_astc ---
[info]:     >32 Registers:              0.00% (0 warps)
[info]:     Full Warp Grid Execution:   100.00% (129601 warps)
[info]:     Warp Branch Divergence:     0.00%
[info]:     16-bit Math Density:        7.04%
[info]:     Instruction Cache Misses:   0
[info]:     ALU Engine Starvation Rate: 113.72%
[info]:   --- L2 Cache: encode_astc ---
[info]:     L2 Cache Read Miss Rate:    33.72%
[info]:     L2 Cache Read Locality:     60.39%
[info]:     Internal Read Stall:        7.89% (992585 cycles)
[info]:   --- External Reads Profile: encode_astc ---
[info]:     Read Transactions (Packets): 1058985
[info]:     Read Beats (Bus Bursts):    4235135
[info]:     Read Payload Data Vol:      67762160 bytes
[info]:     Avg Burst Length:           16.00 bytes/tx
[info]:     External Read Stall Rate:   4.30% (270260 cycles)
[info]:     Slow External Reads (16b): 38.59% (1634344 beats)
[info]:   --- External Read Transaction Queue Load: encode_astc ---
[info]:     Queue Load [ 0% -  25%]:    11.19% (118503 txs)
[info]:     Queue Load [25% -  50%]:    17.96% (190165 txs)
[info]:     Queue Load [50% -  75%]:    28.12% (297809 txs)
[info]:     Queue Load [75% - 100%]:    42.73% (452508 txs)
```

Diff: https://www.diffchecker.com/eGUNjvSB/

Still Heavily LDS bound on Mali GPUs

### Quality

Run with `BCN_DEBUG_ASTC_DIAGNOSTICS=1`

```
[info]:   Diagnostics (87381 blocks, 29318073 total)
[info]:      + mean_error_squared: 0.000000 (PSNR: 76.69), running: 0.000158 (PSNR: 38.01)
[info]:      + mean_quantized_error_squared: 0.000000 (PSNR: inf), running: 0.000004 (PSNR: 53.51)
[info]:      + mean_quantized_error_squared (alt): 0.000000 (PSNR: inf), running: 0.000011 (PSNR: 49.44)
```

(Same as v1)

Across 29,318,073 4x4 blocks encoded by ASTC, the MSE of the encoding runs at ~38.01 dB (PSNR), while both the high precision quantization (53.51 db) and the low precision quantization (49.44 db) are negligible relative to the encoding error.

### Performance

Run with `BCN_PROFILE_MORE_TRANSFERS=1`

```
[info]: Cleaning up batch 86 with 8 buffers, 8 descriptors, and 12 tracked queries took 44 ms
[info]:   [           all] Calls: 4     | Time:  38.73 ms | Data:    3.8 MB | Throughput:   98.4 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      3450.46 ms |        442.8 MB | Throughput:  128.3 MB/s
[info]:             + BC1a (133): 791   |      1978.91 ms |        273.4 MB | Throughput:  138.2 MB/s
[info]:             +  BC3 (137): 598   |      1201.83 ms |        141.1 MB | Throughput:  117.4 MB/s
[info]:             +  BC6 (143): 48    |        10.41 ms |          0.1 MB | Throughput:   12.0 MB/s
[info]:             +  BC7 (145): 48    |       259.31 ms |         28.1 MB | Throughput:  108.4 MB/s
[info]:   [decompress_bcn] Calls: 4     | Time:  13.00 ms | Data:    3.8 MB | Throughput:  293.2 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |       526.65 ms |        442.8 MB | Throughput:  840.7 MB/s
[info]:             + BC1a (133): 791   |       248.92 ms |        273.4 MB | Throughput: 1098.3 MB/s
[info]:             +  BC3 (137): 598   |       201.65 ms |        141.1 MB | Throughput:  699.9 MB/s
[info]:             +  BC6 (143): 48    |         1.81 ms |          0.1 MB | Throughput:   69.2 MB/s
[info]:             +  BC7 (145): 48    |        74.27 ms |         28.1 MB | Throughput:  378.4 MB/s
[info]:   [   encode_astc] Calls: 4     | Time:  24.60 ms | Data:    3.8 MB | Throughput:  155.0 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      2777.75 ms |        442.8 MB | Throughput:  159.4 MB/s
[info]:             + BC1a (133): 791   |      1655.45 ms |        273.4 MB | Throughput:  165.2 MB/s
[info]:             +  BC3 (137): 598   |       939.29 ms |        141.1 MB | Throughput:  150.3 MB/s
[info]:             +  BC6 (143): 48    |         5.67 ms |          0.1 MB | Throughput:   22.1 MB/s
[info]:             +  BC7 (145): 48    |       177.35 ms |         28.1 MB | Throughput:  158.5 MB/s
```

(~same as v1)

On the G615, BCn decompression is around 800-1000 MB/s whlie astc compression is fairly uniform at ~160 MB/s, for a total throughput of ~130 MB/s

## Checkpoint v3 (7/5/26)

This variant uses `uint8_t` instead of `PackedArray<8, N>`.

TL;DR: 3.5x faster than v1/v2, with significantly lower external memory reads. Bottleneck (at runtime) has shifted from LDS to CVT.

WARNING: this version mandates `uint8_t` support on the driver. See `unsupported_mali_devices.csv` for a list of Mali devices that do not support `uint8_t` (pre-VK 1.1 devices).

### Profile

Tag: https://github.com/leegao/bcn_layer/tree/shader-v3

```
Work registers: 32 (100% used at 100% occupancy)
Uniform registers: 28 (21% used)
Shared storage size: 0 bytes
Stack use: 80 bytes
 - Alloca region: 80 bytes
16-bit arithmetic: 38%
 - Idle SIMD lanes: 29%

                                A     FMA     CVT     SFU      LS       T    Bound
Total instruction cycles:   14.23    1.59   12.75    5.94   22.90    0.00       LS
Shortest path cycles:        0.09    0.00    0.09    0.00    0.00    0.00   A, CVT
Longest path cycles:          N/A     N/A     N/A     N/A     N/A     N/A      N/A

A = Arithmetic, FMA = Arith FMA, CVT = Arith CVT, SFU = Arith SFU,
LS = Load/Store, T = Texture
```

See https://github.com/leegao/etc2_encode/actions/runs/27704794287

**Dynamic Profile**

Run with `BCN_SAMPLE_GPU_COUNTERS=1`

```
[info]: --- Profile: encode_astc ---
[info]:   Compute Purity:       100.00%
[info]:   ALU FMA Pipe Util:    3.09%
[info]:   ALU CVT Pipe Util:    54.55%
[info]:   ALU SFU Pipe Util:    18.84%
[info]:   Load/Store Unit Util: 36.99%
[info]:   Compute Queue Cycles: 2523628 cycles (129601 warps)
[info]:   GPU Active Cycles:    2523628 cycles (129601 warps)
[info]:   --- Shader Stats: encode_astc ---
[info]:     >32 Registers:              0.00% (0 warps)
[info]:     Full Warp Grid Execution:   100.00% (129601 warps)
[info]:     Warp Branch Divergence:     0.00%
[info]:     16-bit Math Density:        20.67%
[info]:     Instruction Cache Misses:   0
[info]:     ALU Engine Starvation Rate: 28.06%
[info]:   --- L2 Cache: encode_astc ---
[info]:     L2 Cache Read Miss Rate:    15.86%
[info]:     L2 Cache Read Locality:     75.16%
[info]:     Internal Read Stall:        2.15% (108774 cycles)
[info]:   --- External Reads Profile: encode_astc ---
[info]:     Read Transactions (Packets): 189286
[info]:     Read Beats (Bus Bursts):    756347
[info]:     Read Payload Data Vol:      12101552 bytes
[info]:     Avg Burst Length:           15.98 bytes/tx
[info]:     External Read Stall Rate:   0.60% (15027 cycles)
[info]:     Slow External Reads (16b): 55.60% (420542 beats)
[info]:   --- External Read Transaction Queue Load: encode_astc ---
[info]:     Queue Load [ 0% -  25%]:    24.15% (45710 txs)
[info]:     Queue Load [25% -  50%]:    11.81% (22346 txs)
[info]:     Queue Load [50% -  75%]:    24.66% (46683 txs)
[info]:     Queue Load [75% - 100%]:    39.38% (74547 txs)
```

Diff: https://www.diffchecker.com/EHgDh54q/

60% reduction in overall cycles, significantly higher ratio of non-LDS cycles, almost 1/6 of the memory reads. ALU starvation rate is also significantly reduced.

In fact, the bottleneck has now shifted to CVT

### Quality

Run with `BCN_DEBUG_ASTC_DIAGNOSTICS=1`

```
[info]:   Diagnostics (87381 blocks, 29318073 total)
[info]:      + mean_error_squared: 0.000000 (PSNR: 76.69), running: 0.000158 (PSNR: 38.01)
[info]:      + mean_quantized_error_squared: 0.000000 (PSNR: inf), running: 0.000004 (PSNR: 53.51)
[info]:      + mean_quantized_error_squared (alt): 0.000000 (PSNR: inf), running: 0.000011 (PSNR: 49.44)

```

(Same as v1, v2)

Across 29,318,073 4x4 blocks encoded by ASTC, the MSE of the encoding runs at ~38.01 dB (PSNR), while both the high precision quantization (53.51 db) and the low precision quantization (49.44 db) are negligible relative to the encoding error.

### Performance

Run with `BCN_PROFILE_MORE_TRANSFERS=1`

```
[info]: Cleaning up batch 128 with 8 buffers, 8 descriptors, and 12 tracked queries took 31 ms
[info]:   [           all] Calls: 4     | Time:  27.45 ms | Data:    3.8 MB | Throughput:  138.9 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      1531.44 ms |        442.8 MB | Throughput:  289.1 MB/s
[info]:             + BC1a (133): 791   |       826.78 ms |        273.4 MB | Throughput:  330.7 MB/s
[info]:             +  BC3 (137): 598   |       561.38 ms |        141.1 MB | Throughput:  251.4 MB/s
[info]:             +  BC6 (143): 48    |        10.02 ms |          0.1 MB | Throughput:   12.5 MB/s
[info]:             +  BC7 (145): 48    |       133.26 ms |         28.1 MB | Throughput:  210.9 MB/s
[info]:   [decompress_bcn] Calls: 4     | Time:  15.30 ms | Data:    3.8 MB | Throughput:  249.1 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |       563.79 ms |        442.8 MB | Throughput:  785.4 MB/s
[info]:             + BC1a (133): 791   |       261.13 ms |        273.4 MB | Throughput: 1047.0 MB/s
[info]:             +  BC3 (137): 598   |       214.90 ms |        141.1 MB | Throughput:  656.8 MB/s
[info]:             +  BC6 (143): 48    |         2.01 ms |          0.1 MB | Throughput:   62.2 MB/s
[info]:             +  BC7 (145): 48    |        85.75 ms |         28.1 MB | Throughput:  327.8 MB/s
[info]:   [   encode_astc] Calls: 4     | Time:  11.04 ms | Data:    3.8 MB | Throughput:  345.5 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |       798.62 ms |        442.8 MB | Throughput:  554.4 MB/s
[info]:             + BC1a (133): 791   |       477.08 ms |        273.4 MB | Throughput:  573.1 MB/s
[info]:             +  BC3 (137): 598   |       277.18 ms |        141.1 MB | Throughput:  509.2 MB/s
[info]:             +  BC6 (143): 48    |         4.83 ms |          0.1 MB | Throughput:   25.9 MB/s
[info]:             +  BC7 (145): 48    |        39.53 ms |         28.1 MB | Throughput:  710.9 MB/s
```

compared with the v1,v2 profiles of:

```
[info]: Cleaning up batch 86 with 8 buffers, 8 descriptors, and 12 tracked queries took 44 ms
[info]:   [           all] Calls: 4     | Time:  38.73 ms | Data:    3.8 MB | Throughput:   98.4 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      3450.46 ms |        442.8 MB | Throughput:  128.3 MB/s
[info]:             + BC1a (133): 791   |      1978.91 ms |        273.4 MB | Throughput:  138.2 MB/s
[info]:             +  BC3 (137): 598   |      1201.83 ms |        141.1 MB | Throughput:  117.4 MB/s
[info]:             +  BC6 (143): 48    |        10.41 ms |          0.1 MB | Throughput:   12.0 MB/s
[info]:             +  BC7 (145): 48    |       259.31 ms |         28.1 MB | Throughput:  108.4 MB/s
[info]:   [decompress_bcn] Calls: 4     | Time:  13.00 ms | Data:    3.8 MB | Throughput:  293.2 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |       526.65 ms |        442.8 MB | Throughput:  840.7 MB/s
[info]:             + BC1a (133): 791   |       248.92 ms |        273.4 MB | Throughput: 1098.3 MB/s
[info]:             +  BC3 (137): 598   |       201.65 ms |        141.1 MB | Throughput:  699.9 MB/s
[info]:             +  BC6 (143): 48    |         1.81 ms |          0.1 MB | Throughput:   69.2 MB/s
[info]:             +  BC7 (145): 48    |        74.27 ms |         28.1 MB | Throughput:  378.4 MB/s
[info]:   [   encode_astc] Calls: 4     | Time:  24.60 ms | Data:    3.8 MB | Throughput:  155.0 MB/s (granularity: 76.9ns)
[info]:            + total calls: 1485  |      2777.75 ms |        442.8 MB | Throughput:  159.4 MB/s
[info]:             + BC1a (133): 791   |      1655.45 ms |        273.4 MB | Throughput:  165.2 MB/s
[info]:             +  BC3 (137): 598   |       939.29 ms |        141.1 MB | Throughput:  150.3 MB/s
[info]:             +  BC6 (143): 48    |         5.67 ms |          0.1 MB | Throughput:   22.1 MB/s
[info]:             +  BC7 (145): 48    |       177.35 ms |         28.1 MB | Throughput:  158.5 MB/s
```

3.5x faster than v1/v2

On the G615, BCn decompression is around 800-1000 MB/s whlie astc compression is fairly uniform at ~550 MB/s, for a total throughput of ~290 MB/s

In fact, encode_astc is almost equivalent to decompress_bcn, and in fact, encode_astc is now faster than BC6/BC7 due to significantly lower divergent warp rates.
