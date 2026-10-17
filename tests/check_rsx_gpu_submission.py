"""Check immutable submitted metadata and reproduce the live-array failure."""
import subprocess
import sys
import tempfile

for legacy in (False, True):
    with tempfile.TemporaryDirectory(prefix="rsx-submission-") as directory:
        result = subprocess.run([sys.argv[1], directory,
            *(["--reuse-live-records"] if legacy else [])],
            capture_output=True, text=True, timeout=15)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode != int(legacy) or f"checks=11 failures={int(legacy)}" not in result.stdout:
            raise SystemExit("Submitted metadata retention or negative control failed")
