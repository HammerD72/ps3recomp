# Optional SPU execution observations

`PS3RECOMP_EXECUTION_OBSERVATION` defaults OFF. In that configuration the
observation macros discard their arguments; ordinary builds acquire no probe
branches or clocks. Profile owners must configure every participating generated
unit consistently and validate coverage rather than assuming the runtime flag
also rebuilt an unrelated object library.

The immutable observer is installed/cleared only with producers quiescent and
synchronized before resuming. Callbacks can run concurrently. They must not
change guest state, reenter execution, allocate, format, perform I/O or take
application locks. An optional predicate filters before clock/event construction;
the owner must expose exclusions. Zero clocks are flagged; the owner checks
chronology, loss, lifetime identities, missing edges and cross-thread ordering.

The observations are raw boundaries:

- Interpreter invocation can subsequently admit a native leaf. Control-flow
  events give the instruction's actual resulting PC, including fallthrough.
- Trampoline/scoped dispatch reports the selected function after interrupt
  resolution and before calling it. Final lookup calls additionally carry
  `RESOLVED_TARGET`; the indirect resolver's required `musttail` remains intact.
- Direct return macros retain the real target and host-frame/trampoline
  behavior. Their source instruction PC is unavailable. Register-index
  expressions are evaluated once.
- Accepted native execution emits begin/end around execution and continuation
  validation. Preparation cost is outside this pair. Observation does not set
  the interpreter's legacy instrumentation gate or disable native admission.

These do not establish job identity, prerequisites, completion or exclusive
CPU cost. Direct generated calls without a dispatcher, interrupt longjmp/fault
paths and uninstrumented units remain explicit coverage obligations. A call
boundary does not establish the instant its first instruction executed.

`python tools/test_spu_execution_observer.py --mutants --syntax` compiles the
actual interpreter, return/dispatch macros, observer and float helpers. It also
tests the extracted actual scoped dispatch body and final indirect-call block
with authored lookup/channel/IRQ dependencies, retaining the latter's `musttail`.
It checks modes 0/1, source/result PCs, conditional fallthrough, interrupt
selection, both return paths, selected output values, native admission,
filtering and invalid clocks. Eight compiled mutants must fail their specific
assertions. Full C translation units and a C++ consumer are syntax checked.
This is not full IRQ/coherence verification, capture-overhead measurement or a
game timeline/performance result.
