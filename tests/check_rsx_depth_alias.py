"""Game-free D24S8 alias bytes and stale-source negative control."""
import subprocess
import sys

for mode in [[], ["--hardware"]]:
    for control, expected_code, failures in [([], 0, 0), (["--legacy"], 1, 1)]:
        result = subprocess.run([sys.argv[1], *mode, *control], capture_output=True,
                                text=True, timeout=40)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if (result.returncode != expected_code or
                f"checks=7 failures={failures}" not in result.stdout):
            raise SystemExit("D24 alias pixel or negative-control mismatch")
