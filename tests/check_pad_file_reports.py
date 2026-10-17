"""Production cellPad reports and a historical consume-drain control."""
import subprocess
import sys
import tempfile
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("usage: check_pad_file_reports.py NORMAL_EXE LEGACY_EXE")
with tempfile.TemporaryDirectory(prefix="pad-file-reports-") as directory:
    for legacy, executable in enumerate(sys.argv[1:]):
        result = subprocess.run(
            [executable, str(Path(directory) / ("command%d.txt" % legacy))],
            capture_output=True, text=True, timeout=15,
        )
        print(result.stdout, end="")
        print(result.stderr, end="")
        assert result.returncode == legacy, (executable, result.returncode)
        expected = "pressed=%d checks=6 failures=%d" % (1 if legacy else 4, legacy)
        assert expected in result.stdout, (executable, result.stdout)
