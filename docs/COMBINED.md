# Combined runtime testing

The `combined` branch in [robbe1912/ps3recomp](https://github.com/robbe1912/ps3recomp/tree/combined)
is the integration lane for shared fixes. Individual implementation commits and
their original branches remain available for later selective upstream proposals.
Do not submit this cumulative branch as one upstream PR. Upstream publication
requires the owner's approval.

## Included changes

Upstream baseline: `a2c20108af45174dbb7d3e2f87f6acaaef09c6b4` (v0.14.0),
merged at `8b244cf`. This replaces the earlier `5244db9` comparison point.

| Area | Implementation commits | Boundary |
| --- | --- | --- |
| Allocation alias protection / physical capacity | `8ad9fdb`, `dcd6f48` | Virtual alias gaps do not consume physical capacity; alias protection is a dependency. |
| Indexed strip/fan restart | `7db6730` | Handle restart before attribute fetch and preserve split-batch continuity. |
| Packed vertex attributes | `0b85442` | Decode CMP and packed components through the common fetch path. |
| Fragment source-2 absolute | `0d03c8b` | Apply DWORD3 absolute before negation; authored actual-decompiler regression `27559e1`. |
| CPU/device surface reads | `77cab17` | Acquire device-written pages through shared read synchronization. |
| AA surface layout | `12a158f` | Derive sample-grid extent from AA mode, independently of pitch padding. |
| SPURS taskset guest layout | `3ed7f07`, `34acdab` | Guest mask/field layout, including authored bitset checks. |
| Task dispatch ownership | `f7104c0`, `fda09a4` | Preserve explicit task identity through dispatch; adapter exercises the actual source. |
| Task exit/incarnations | `35239df`, `9bfe08d`, `8e40617` | Distinguish EXIT from entry return, reject stale ownership, release unlaunched claims. |
| Fragment HLSL capacity | `387d1fa` / upstream `75a3269` | Original atomic branch retained; also present in the upstream integration. |
| D3D12 window visibility | `d316fbc` / original `92885d4` | Show the host game window after creation. |

The inherited opt-in task tracing (`dbe0b4b`) is diagnostic, not a correctness
fix. Unfinished PVS/draw observers and the force-updated pointer-audit branch
are excluded. Merge presence alone is not evidence of a native game fix.

## Integration checks, 2026-10-14

On Windows x64, Clang 17.0.3, Ninja Release with
`PS3RECOMP_BUILD_TESTS=ON` and `BUILD_TESTING=ON`:

- Runtime build and all seven CTest tests pass: CPU-surface HLE, sync stress,
  three condition tests, fiber switching and codec stub.
- All 24 `tools/test_*.py` commands exit successfully. Interpreter ILH explicitly
  skips on Windows; successful exit is not coverage of its POSIX execution.
- Separate production-source fixtures pass indexed restart, packed vertex fetch,
  AA extent and signed homogeneous-W clipping with WARP pixel readback.
- The fork-native source-2 fixture passes; restoring the old missing-absolute
  decode makes it fail. It uses this tree's seven-argument shader API and calls
  constant collection separately. A prior cross-tree test incorrectly supplied
  the MotorStorm header's eight-argument API; that result is invalid.

Standalone fiber and codec test targets now compile `guest_read_sync.c` because
their inline guest reads reference its state/dispatcher. Linux/macOS direct SPU
compile lists include the task-lifecycle module alongside workload code.
All four CI workflows run on pushes to `combined`; hosted results remain the
authority for platforms not executed locally.

These are authored/runtime tests without original game inputs. No native
MotorStorm run was performed with this merged fork, no speedup was measured,
and no bike/menu/race acceptance follows. FFmpeg decoding was not enabled in
the local CTest configuration. Keep source/API admission, runtime semantics,
native visuals and gameplay acceptance separate when selecting changes.
