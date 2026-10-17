"""Color-only zeta capacity control plus real depth/stencil pixel contracts."""
import subprocess
import sys

for mode in [[], ["--hardware"]]:
    for control, code, failures in [([], 0, 0), (["--eager-depth"], 1, 1)]:
        result = subprocess.run([sys.argv[1], *mode, *control], capture_output=True,
                                text=True, timeout=40)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if (result.returncode != code or
                f"checks=15 failures={failures}" not in result.stdout):
            raise SystemExit("Depth/stencil pixel or unused-attachment control mismatch")
