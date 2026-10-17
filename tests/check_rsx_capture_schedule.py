"""Cadence contracts plus historical batch-clock negative control."""
import subprocess
import sys
for args, expected, failures in (([], 0, 0), (["--batch-clock"], 1, 1)):
    result = subprocess.run([sys.argv[1], *args], capture_output=True, text=True, timeout=10)
    print(result.stdout, end="")
    if result.returncode != expected or f"checks=19 failures={failures}" not in result.stdout:
        raise SystemExit(result.stderr or "unexpected capture-cadence result")
