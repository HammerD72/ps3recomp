"""GPU addressing, old-state control and actual table-helper retirement checks."""
import subprocess
import sys


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    return result


gpu, tables = sys.argv[1:]
positive = run([gpu])
assert positive.returncode == 0 and positive.stdout.count(" PASS") == 7
negative = run([gpu, "--unpatched"])
assert negative.returncode == 1 and negative.stdout.count(" FAIL") == 6
assert negative.stdout.count(" PASS") == 1
run([tables]).check_returncode()
failed = run([tables, "--fail-retire"])
assert failed.returncode != 0 and "GPU retirement failed" in failed.stderr
print("PASS: seven GPU cases, six old-state failures and checked table lifetime")
