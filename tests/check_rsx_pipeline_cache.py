"""Production pipeline storage and the historical 256-entry control."""
import subprocess
import sys
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "libs/video/rsx_d3d12_backend.c").read_text()
assert "rsx_pipeline_cache_acquire(&s_d3d.vp_fp_store" in source
assert "memmove(&s_d3d.vp_fp[0]" not in source
for legacy in (False, True):
    result = subprocess.run([sys.argv[1], *(["--legacy"] if legacy else [])],
                            capture_output=True, text=True, timeout=15)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    assert result.returncode == int(legacy), result
    assert ("legacy_misses=3000 checks=1 failures=1" if legacy else
            "checks=10 failures=0 warm_misses=0") in result.stdout
