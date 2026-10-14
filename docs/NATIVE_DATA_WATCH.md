# Native Windows data watch

`tools/windows_data_watch.py` observes CPU writes to one through four real,
eight-byte-aligned host addresses in an owned 64-bit Windows process. Supply a
private launch JSON containing `pid`, `create_time` (Unix seconds), `exe` and
`exe_sha256`. The observer verifies the live handle's creation time and image
path, plus the image file hash, before attaching. Integer FILETIME epoch
subtraction preserves the timestamp representation used by launch manifests.

Example, with an actual address resolved from the owned process:

```
python tools/windows_data_watch.py --owner private-owner.json --address HOST_ADDRESS --out temp/checks/watch --seconds 20 --events 64 --dump-nonzero 1
```

The observer sets hardware write breakpoints on existing and newly created
threads. It does not patch target memory or guest instructions. Each hit records
the actual post-store RIP, writing thread, integer registers, breakpoint mask and
watched bytes while the debug event holds the process stopped. Optional dumps
retain thread information and normal minidump stack memory; they are not full
process-memory captures. These artifacts can contain private target data and
belong outside version control.

The event/duration budget ends observation. Existing debug-register values are
restored while the final debug event is held, before continuation and detach.
A deadline without writes requests a cleanup breakpoint. Unrelated application
exceptions are passed to the application. Require an empty `cleanup_errors` list
and no `failure` in `final.json` before treating a capture as successful. A failed
observer attachment or cleanup is not evidence of a target crash.

The hardware address identifies a CPU writer, not every possible writer: a
different virtual alias or a device write need not trigger it. A writer hit alone
does not establish the target's root cause, submission identity or completion.
Attaching, stopping and dumping affect timing; do not measure performance here.

Run `python tools/test_windows_data_watch.py` in a Windows compiler environment
(`MV_CLANG` optional, otherwise `clang-cl` on PATH). The authored fixture checks
four exact writes across the original thread and a thread created after attach,
a retained hit dump, successful writes after detach, a no-hit deadline, and the
FILETIME conversion regression. Other platforms explicitly skip this Windows
observer fixture. No game inputs are needed.
