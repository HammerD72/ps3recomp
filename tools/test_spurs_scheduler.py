#!/usr/bin/env python3
"""Run real bounded SPURS scheduling with manufactured policy modules only."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mutants', action='store_true', help='Reject serialized-admission regression')
parser.add_argument('--compiler', default=os.environ.get('CC') or
                    shutil.which('clang-cl' if os.name == 'nt' else 'cc'))
args = parser.parse_args()
assert args.compiler, 'Set CC or --compiler to a C17 compiler'
out = root / 'build/spurs-scheduler-tests'
out.mkdir(parents=True, exist_ok=True)
units = ['spu_channels','spu_drain','spu_lockstep','spu_coherency','spu_workload',
         'spurs_policy','spurs_job','spu_tsp_weak','spu_vm_pagemap',
         'spurs_policy_blob_weak','spu_interp','spu_float_simd','spu_task_lifecycle']
exe = out / ('fixture.exe' if os.name == 'nt' else 'fixture')
command = [args.compiler, str(root/'tests/test_spurs_scheduler.c'),
           *[str(root/'runtime/spu'/f'{unit}.c') for unit in units],
           str(root/'runtime/platform/win32_compat.c'),
           str(root/'runtime/memory/guest_read_sync.c'),
           str(root/'runtime/platform/thread_name.c')]
includes = [root/'include', root/'runtime/spu', root/'runtime/platform']
if os.name == 'nt':
    command += ['/nologo','/std:c17','/O2','/UNDEBUG','/Gy','/Gw',
                *['/I'+str(path) for path in includes], '/Fo'+str(out)+os.sep,
                '/Fe'+str(exe), '/link','/OPT:REF']
else:
    command += ['-std=gnu17','-D_GNU_SOURCE','-O1','-UNDEBUG','-ffunction-sections','-fdata-sections',
                '-Wl,-dead_strip' if platform.system()=='Darwin' else '-Wl,--gc-sections',
                *['-I'+str(path) for path in includes], '-pthread','-lm','-o',str(exe)]
env = {key:value for key,value in os.environ.items()
       if not key.startswith(('SPU_','SPURS_','YDKJ_','LBP_','GH3_'))}
(out/'command.json').write_text(json.dumps(command, indent=2))
built = subprocess.run(command, env=env, cwd=out, capture_output=True, text=True)
(out/'build.log').write_text(built.stdout+built.stderr)
assert built.returncode == 0, built.stdout+built.stderr
ran = subprocess.run([str(exe)], env=env, cwd=out, capture_output=True, text=True, timeout=30)
(out/'run.log').write_text(ran.stdout+ran.stderr)
print(ran.stdout, end='')
print(ran.stderr, end='')
assert ran.returncode == 0, 'Actual scheduler fixture failed; retain run.log'
assert 'SPURS same workload/lane across instances retains separate LS and TLS exit classification PASS' in ran.stdout
print('PASS actual scheduler on '+platform.system()+'; no title inputs or skipped execution')

if args.mutants:
    for name,failure in [
        ('serialized-admission','runnable producer could not run'),
        ('consumed-ready-demand','scheduler consumed policy-owned requested lanes'),
        ('stale-rejected-policy','rejected policy retained preceding worker productivity')]:
        mutant=out/name
        (mutant/'tests').mkdir(parents=True,exist_ok=True)
        (mutant/'libs/spurs').mkdir(parents=True,exist_ok=True)
        (mutant/'runtime/spu').mkdir(parents=True,exist_ok=True)
        shutil.copy2(root/'tests/test_spurs_scheduler.c',mutant/'tests/test_spurs_scheduler.c')
        source=(root/'libs/spurs/cellSpurs.c').read_text()
        policy=(root/'runtime/spu/spurs_policy.c').read_text()
        if name=='serialized-admission':
            start=source.index('static DWORD WINAPI spurs_kernel_thread(LPVOID p)\n{')
            before='for (u32 sn = 0; sn < si->nspus; sn++)'
            assert before in source[start:]
            source=source[:start]+source[start:].replace(before,'for (u32 sn = 0; sn < 1; sn++)',1)
        elif name=='consumed-ready-demand':
            before='                /* Requested lanes are policy-owned demand'
            assert source.count(before)==1
            source=source.replace(before,'                if (ready) vm_base[ea + SPURS_WKL_READY1 + wid] = (u8)(ready - 1);\n'+before)
        else:
            before='    g_spurs_pm_polls = 0;\n    g_spurs_pm_exited = 0;\n    g_wws_batch_gets = 0;\n'
            assert policy.count(before)==1
            policy=policy.replace(before,'')
            admission='    if (wid >= SPURS_PM_MAX_WKL || spu_num >= 6) return -1;'
            assert policy.count(admission)==1
            policy=policy.replace(admission,before+admission)
        (mutant/'libs/spurs/cellSpurs.c').write_text(source)
        (mutant/'runtime/spu/spurs_policy.c').write_text(policy)
        mutant_exe=mutant/('fixture.exe' if os.name=='nt' else 'fixture')
        replacements={str(root/'tests/test_spurs_scheduler.c'):str(mutant/'tests/test_spurs_scheduler.c'),
                      str(root/'runtime/spu/spurs_policy.c'):str(mutant/'runtime/spu/spurs_policy.c'),
                      '/Fe'+str(exe):'/Fe'+str(mutant_exe),str(exe):str(mutant_exe),
                      '/Fo'+str(out)+os.sep:'/Fo'+str(mutant)+os.sep}
        mutant_command=[replacements.get(arg,arg) for arg in command]
        extra=[('/I' if os.name=='nt' else '-I')+str(root/path) for path in ('tests','libs/spurs')]
        position=mutant_command.index('/link') if os.name=='nt' else len(mutant_command)
        mutant_command[position:position]=extra
        (mutant/'command.json').write_text(json.dumps(mutant_command,indent=2))
        built=subprocess.run(mutant_command,env=env,cwd=mutant,capture_output=True,text=True)
        (mutant/'build.log').write_text(built.stdout+built.stderr)
        assert built.returncode==0, 'Mutant must compile: '+str(mutant/'build.log')
        ran=subprocess.run([str(mutant_exe)],env=env,cwd=mutant,capture_output=True,text=True,timeout=15)
        (mutant/'run.log').write_text(ran.stdout+ran.stderr)
        assert ran.returncode!=0 and failure in ran.stderr, 'Mutant escaped or failed for another reason: '+name
        if name=='serialized-admission':
            print('PASS serialized-admission mutant compiled and failed the real producer-progress assertion')
        else:
            print('PASS '+name+' mutant compiled and failed its intended behavioral assertion')
