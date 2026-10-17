"""Check recorded object ownership and the cache-eviction failure."""
import subprocess
import sys
for legacy in (False, True):
    p = subprocess.run([sys.argv[1], *(["--no-recorded-reference"] if legacy else [])], capture_output=True, text=True, timeout=15)
    print(p.stdout, end="")
    print(p.stderr, end="", file=sys.stderr)
    if p.returncode != int(legacy) or f"checks=11 failures={int(legacy)}" not in p.stdout:
        raise SystemExit("Recorded object reference or cache-eviction control failed")
