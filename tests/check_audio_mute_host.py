"""Verify two real Windows session mute/init/shutdown cycles (requires audio)."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('--log', type=Path, required=True)
args = parser.parse_args()
if os.name != 'nt':
    parser.error('Windows audio-session contract requires Windows')
env = dict(os.environ, AUDIO_MUTE='1')
proc = subprocess.run([str(args.executable.resolve()), '--audio-pad'], env=env,
                      capture_output=True, text=True, timeout=30)
log = proc.stdout + proc.stderr
args.log.parent.mkdir(parents=True, exist_ok=True)
args.log.write_text(log, encoding='utf-8')
assert proc.returncode == 0, log
assert log.count('[cellAudio] AUDIO_MUTE session verified: muted=1') == 2, log
assert 'audio and pad backends came up and down twice OK' in log, log
assert 'WARNING' not in log and 'failed' not in log.lower(), log
print('PASS two real WASAPI session mute/init/shutdown cycles')
