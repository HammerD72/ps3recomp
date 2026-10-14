## Productive IRET continuation regression

`tools/test_spu_iret_progress.py` executes the real interpreter against an
authored CPU-only program. It increments a register, then executes IRET to the
same continuation seven times before its own conditional branch reaches STOP.
The expected result is eight increments and architectural STOP, without a host
halt. CPU channel/lift/recovery dependencies trap if accidentally reached.

The combined fork already preserves that behavior. The curated native runtime
had an added heuristic that counted equal IRET return PCs as idleness: it parked
this productive program after five increments. Repeated return addresses do
not prove absence of intervening guest work. This regression preserves that
architectural distinction without game inputs, original bytes or target IDs.

The runner supports `--runtime` for a compatible runtime/SPU directory, compiles
with that directory's actual headers, and retains source and executable hashes
and build/run logs in ignored `temp/checks/iret-progress-*`. Windows uses the
configured `MV_CLANG` override or `clang-cl` on PATH; POSIX uses CC/cc.
This CPU regression does not prove native game or race acceptance.
