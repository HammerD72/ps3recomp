# Memory capacity trial

This branch preserves the existing local allocation alias guard separately from the candidate correction. Its synthetic fixture uses actual VM initialization, page commitment and syscall handlers; it contains no guest payload.

On the preserved baseline, six 24 MiB allocations succeed. The seventh returns CELL_ENOMEM although sys_memory_get_user_memory_size reports 69 MiB available. Collision gaps exhaust the 256 MiB virtual arena before the advertised 213 MiB physical pool.

The candidate provides a 512 MiB virtual arena, checks the existing 213 MiB physical budget independently, and permits registered aliases above the old arena end. The Windows clang-cl fixture passes eight allocations, checks their registered aliases, then verifies that a ninth allocation fails without modifying the output pointer or available-memory count. Windows and Linux CI steps run that fixture; remote CI and Linux validation remain pending.

This is a component trial, not a demonstrated native-game fix. The selected guest malloc's syscall/failure branch remains uncaptured. No substitution, null-write override or forced successful allocation is used. Reclamation, registration lifecycle/capacity and commit-failure accounting remain inherited limitations; widening the virtual arena does not prove those contracts.
