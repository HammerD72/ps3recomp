"""Run authored lifecycle/concurrency and actual runner-result fixtures."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runtime', type=Path, default=ROOT)
    args = ap.parse_args(); runtime = args.runtime.resolve()
    out = ROOT/'temp/checks'/datetime.datetime.now(datetime.timezone.utc).strftime('lifecycle-%Y%m%dT%H%M%S.%fZ'); out.mkdir(parents=True)
    report = []
    for name in ['task_lifecycle', 'task_end_reason']:
        fixture = ROOT/'tests'/('test_'+name+'.c'); inputs = [fixture]
        if name == 'task_lifecycle': inputs.append(runtime/'runtime/spu/spu_task_lifecycle.c')
        includes = [ROOT/'tests',runtime/'include',runtime/'runtime/spu',runtime/'runtime/platform']
        if os.name == 'nt':
            exe = out/(name+'.exe')
            command = [os.environ.get('MV_CLANG','clang-cl'),'/nologo','/TC','/O2','/UNDEBUG',*[f'/I{p}' for p in includes],*map(str,inputs),f'/Fo{out}/',f'/Fe{exe}']
        else:
            exe = out/name
            command = [os.environ.get('CC','cc'),'-std=gnu17','-D_GNU_SOURCE','-UNDEBUG','-O2','-pthread',*[f'-I{p}' for p in includes],*map(str,inputs),'-o',str(exe)]
        build = subprocess.run(command,capture_output=True,timeout=120); (out/(name+'-build.log')).write_bytes(build.stdout+build.stderr)
        if build.returncode:
            print('\n'.join(x for x in (build.stdout+build.stderr).decode(errors='replace').splitlines() if 'error' in x)); raise SystemExit(build.returncode)
        run = subprocess.run([str(exe)],capture_output=True,timeout=30); (out/(name+'-run.log')).write_bytes(run.stdout+run.stderr)
        print((run.stdout+run.stderr).decode(errors='replace')); run.check_returncode()
        paths = inputs + [runtime/'runtime/spu/spu_task_lifecycle.h',ROOT/'tests/task_vm_fixture.h',runtime/'runtime/spu/spu_lifted_job.h',runtime/'runtime/spu/spu_context.h']
        report.append({'command':command,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'returncode':run.returncode})
    (out/'result.json').write_text(json.dumps(report,indent=2)); print('Evidence:',out)
if __name__ == '__main__': main()
