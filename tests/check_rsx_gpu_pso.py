"""Execute recorded draws after eviction of their original PSO cache owners."""
import subprocess
import sys
for hardware in (False, True):
    p = subprocess.run([sys.argv[1], *(["--hardware"] if hardware else [])], capture_output=True, text=True, timeout=40)
    print(p.stdout, end="")
    print(p.stderr, end="", file=sys.stderr)
    if p.returncode or "checks=6 failures=0" not in p.stdout:
        raise SystemExit("Recorded PSO execution failed")
