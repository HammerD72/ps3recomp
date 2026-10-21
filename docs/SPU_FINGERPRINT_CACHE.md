# Exact SPU fingerprint cache

`SPU_FINGERPRINT_CACHE=1` opts into memoizing the existing workload fingerprint.
Default behavior is uncached. This preserves the existing FNV offset basis,
prime and result; it does not change registered content identities or dispatch.

Only4096–262144-byte buffers qualify. Each thread owns at most32 snapshots and
4MiB of retained snapshot bytes, plus metadata. A matching address/size is only
a candidate: **every byte** must match the owned snapshot before reusing its
hash. Changed bytes recompute the original hash from a refreshed owned copy.
That copy is also the hash input, preventing a hash/snapshot association from
two separate source reads. Pointer reuse, changed LS contents and eviction
cannot authorize stale hits. Callers must keep their buffers stable during a
call, as required by the existing fingerprint API; neither implementation
promises atomic observations of concurrent writes.

Windows FLS/POSIX thread-key destructors release snapshots when workers exit.
Optional cache allocation/setup failures are visible and return an exact
uncached result. No sparse byte checks or global positive-match shortcut is
used. `SPU_FINGERPRINT_STATS=1` emits cumulative per-cache/thread counts at
powers of two and destruction, without per-call clocks. Comparison-span bytes
count requested full comparisons, not how many bytes an early-exit memcmp read.

`python tools/test_spu_fingerprint_cache.py` compiles the actual production
wrapper and raw routine, checks independent Python-computed hashes, disabled
mode, boundary/interior changes, memory bounds, eviction, allocation failures,
thread isolation and real thread-exit cleanup. `--production-source PATH`
can demonstrate that an unchanged uncached historical wrapper fails the
expected reduction in raw hash calls. These are correctness checks, not game
FPS measurements. Native usefulness and default enablement require workload
measurement; interpretation requirements and renderer behavior are unchanged.
