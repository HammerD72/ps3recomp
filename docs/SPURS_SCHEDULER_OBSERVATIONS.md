# SPURS scheduler observations

`PS3RECOMP_SPURS_SCHEDULER_OBSERVATION` defaults OFF. Ordinary builds exclude
identity fields, counters, admission snapshots, TLS updates and probe clocks
from the scheduler. Enabling it does not install an observer automatically.

Install `ps3_spurs_scheduler_observer` only while all producers are quiescent,
with synchronization before resuming. Keep observer/user storage immutable and
alive. Callbacks can run concurrently, outside the instance, workload-registry,
directory and coherence locks. They must return normally without guest writes,
runtime reentry, allocation, formatting, file I/O or application locks. Copy
event values during the callback if needed afterward. Rejecting setup is an
error the capture owner must report. Filtering is an explicit coverage limit.

Instance reservation has a process-local monotonically increasing ID. Workload
registration gets a new generation even when the same slot, code and argument
are reused. Each instance numbers actual policy admissions. Counters saturate;
exhaustion yields zero with an invalid-identity flag rather than wrapping or
changing the guest result. Add a capture run identity before combining traces.
Initialization has separate reserved/result records; reservation is not success.
A successful close retires the instance and its remaining workloads. Workload
removal has a record only after successful removal. Failed/BUSY API attempts
are not represented as lifecycle transitions.

For each admission pass, a clock is sampled before acquiring the instance lock.
The scheduler copies admitted assignments and observed ready/signal/contention
values under its existing locks into a bounded six-element local batch. It
delivers the batch after releasing those locks. Each admission therefore has
an interval, not an exact timestamp. Several assignments can share its start.
A worker can begin and return before the coordinator delivers its admission
record. Never infer commit ordering, OS ready latency or prerequisite readiness
from record arrival order or the interval's end. Demand values are sequential
observations, not a coherent global snapshot. Zero-demand backoff reentry
remains possible. The coordinator neither consumes policy-owned ready counts
nor changes its existing sleep/admission/priority/contention behavior.

Worker records bracket the actual `spu_run_policy_module` call, outside the
instance lock. Return records carry its genuine result and productive-poll
counter. A policy invocation can handle several jobs, no productive job, or a
stop/error; this is not each job's completion. Faults/unwind do not invent a
return event. `ps3_spurs_current_dispatch` copies this thread's current host
assignment between those boundaries, and leaves the caller's output untouched
when none exists. Direct policy calls outside the pool have no host assignment.
An unrelated/nested SPU context does not acquire ownership merely because it
runs on that thread. Bind original context/job headers and queue generations
separately before treating this as a job dependency or critical path.

All clocks must belong to the capture owner's single monotonic domain. Zero or
reversed admission intervals are flagged. Lifecycle samples follow operations;
they do not identify an exact earlier publication instant. Missing lifecycle,
filtered paths, owner overflow, partial windows and unknown dependencies must
remain visible in the analyzer. Installation and storage do not provide a
game capture owner or complete job timeline.

`tools/test_spurs_scheduler.py` retains the real scheduler/policy execution with
authored policies and dependencies, plus its existing regression controls.
`--observe --observer-mutants --syntax` additionally checks instance/workload
reuse, actual physical-lane/assignment metadata, admission/begin/return matching,
real stop results, direct-call absence, invalid clocks, identity exhaustion,
filtering and same-thread lock witnesses. Production C and C++ header syntax
are checked. Nine compiled observer faults must fail their specific assertions:
reused instance/workload IDs, wrong lane, missing admission/return, callback under
lock, hidden clock/exhaustion and false return result. Compiler, setup and timeout
failures do not count as negative-control success. Authored lock/VM dependencies
are not proof of whole-game synchronization correctness.

No game owner, queue/job binding, capture overhead, end-to-end frame time or
native gameplay performance is established by these tests. Those remain separate
acceptance gates before using a trace to change admission or replace a whole
game operation with a typed native batch.
