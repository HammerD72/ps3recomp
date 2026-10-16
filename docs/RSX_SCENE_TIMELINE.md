# Opt-in scene readbacks

`RSX_TIMELINE_ARM_FILE` names a once-only arm file containing a hexadecimal
offscreen target offset followed by one to eight strictly increasing decimal
operation indices. `RSX_CAP_DIR` selects a private output directory. Nothing
is enabled without the environment variable and arm file.

Copies execute before those operations in the original command list. They
restore the prior tracked resource state and require checked GPU retirement
before mapping. No draws are skipped or replaced. The allocation budget is
128 MiB; invalid input or failed retirement is an explicit diagnostic failure.

Each BMP now has `.raw` and `.layout` sidecars. Raw bytes retain the mapped
resource's alpha, packed bits and floating-point values; BMP visualization does
not. Layout records target, width, height, aligned row pitch, numeric DXGI
format and byte count. Interpret only pixel bytes: row padding is unspecified.
This is the host resource representation, not a guest-memory dump. Output
paths must not already exist; open/write/close failures remain fatal.

Captures and any original payloads must remain private. Readback copies and
disk writes perturb execution, so diagnostic runs are not performance proof.
