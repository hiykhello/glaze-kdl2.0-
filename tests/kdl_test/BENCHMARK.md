# KDL scanning benchmark

Local observation, 2026-10-27. Intel Core i9-13900H, Clang 23.1.4,
C++23, `-O3 -DNDEBUG`. Each row reports the median of five samples from
`kdl_benchmark.cpp`; the binaries were run sequentially after builds/tests ended.
No sanitizer or simultaneous benchmark process was used. These are in-memory
microbenchmarks, not disk throughput or guarantees on other hardware.

Build from the repository root:

```sh
clang++ -std=c++23 -O3 -DNDEBUG -Iinclude tests/kdl_test/kdl_benchmark.cpp -o /tmp/kdl-benchmark-sse2
clang++ -std=c++23 -O3 -DNDEBUG -mavx2 -Iinclude tests/kdl_test/kdl_benchmark.cpp -o /tmp/kdl-benchmark-avx2
clang++ -std=c++23 -O3 -DNDEBUG -DGLZ_DISABLE_SIMD -Iinclude tests/kdl_test/kdl_benchmark.cpp -o /tmp/kdl-benchmark-scalar
/tmp/kdl-benchmark-scalar
/tmp/kdl-benchmark-sse2
/tmp/kdl-benchmark-avx2
```

| Workload | Scalar MB/s | SSE2 MB/s | AVX2 MB/s |
|---|---:|---:|---:|
| Read small configuration | 166.1 | 182.6 | 174.5 |
| Read 64 KiB unescaped string | 1381.0 | 10140.0 | 18892.7 |
| Read 64 KiB block comment | 1700.2 | 13322.9 | 27619.6 |
| Read 64 KiB whitespace | 1684.5 | 11031.6 | 28262.6 |
| Read 64 reflected fields, hash dispatch | 139.4 | 145.1 | 147.8 |
| Write 64 KiB string | 2402.1 | 18705.2 | 32155.4 |

The 64-field case takes 5366.4 / 5156.7 / 5061.4 ns per document,
respectively. All three use the same hash lookup and grammar, so this comparison
does not isolate hash dispatch against a linear lookup.

Long ASCII runs benefit most; short fields and configuration parsing remain
mostly grammar/dispatch work. SIMD speedups do not imply equivalent gains on
Unicode-heavy or escaped text. Allocation and memory-copy costs remain.

SSE2 and AVX2 scanner differential/boundary tests were executed locally;
AVX512BW compiled successfully but could not execute on this host. NEON/WASM
implementations were not executed on this x86 host.
