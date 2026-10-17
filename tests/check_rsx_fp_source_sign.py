"""Check source sign bits and retain FXC's historical invalid-bytecode control."""
import subprocess
import sys

for adapter in ([], ["--hardware"]):
    for legacy in (False, True):
        result = subprocess.run(
            [sys.argv[1], *adapter, *(["--legacy-negate"] if legacy else [])],
            capture_output=True, text=True, timeout=40)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if legacy:
            if (result.returncode != 2 or "Invalid Bytecode" not in result.stderr
                    or "Negate modifier not allowed" not in result.stderr):
                raise SystemExit("Legacy source negation no longer reproduces FXC failure")
        elif result.returncode != 0 or "checks=16 failures=0" not in result.stdout:
            raise SystemExit("Source sign bits or partial-word consumer mismatch")
