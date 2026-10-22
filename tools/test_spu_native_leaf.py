#!/usr/bin/env python3
"""Build the actual interpreter and authored leaf tests; reject unsafe mutants."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
spu = root / 'runtime/spu'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default=os.environ.get('CC') or shutil.which('clang-cl' if os.name=='nt' else 'cc'))
args = parser.parse_args()
assert args.compiler, 'Set CC or --compiler'
out = root / 'build/native-leaf-tests'
out.mkdir(parents=True, exist_ok=True)
env = {k:v for k,v in os.environ.items() if not k.startswith(('SPU_','SPURS_','YDKJ_','LBP_','GH3_'))}

def build(name, source):
    directory=out/name
    directory.mkdir(exist_ok=True)
    exe=directory/('fixture.exe' if os.name=='nt' else 'fixture')
    command=[args.compiler, str(source), str(spu/'spu_float_simd.c'), str(spu/'tests/spu_native_leaf_test.c')]
    if os.name=='nt':
        command+=['/nologo','/std:c17','/O2','/UNDEBUG','/D_CRT_SECURE_NO_WARNINGS',
                  '/I'+str(root/'include'),'/I'+str(spu),'/Fo'+str(directory)+os.sep,'/Fe'+str(exe)]
    else:
        command+=['-std=gnu17','-O2','-UNDEBUG','-I'+str(root/'include'),'-I'+str(spu),'-lpthread','-lm','-o',str(exe)]
    (directory/'command.json').write_text(json.dumps(command,indent=2))
    r=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True)
    (directory/'build.log').write_text(r.stdout+r.stderr)
    assert r.returncode==0, r.stdout+r.stderr
    return exe

def run(exe, mode='normal', extra=None):
    r=subprocess.run([str(exe),mode],cwd=exe.parent,env=dict(env,**(extra or {})),capture_output=True,text=True,timeout=20)
    (exe.parent/(mode+'.log')).write_text(r.stdout+r.stderr)
    return r

exe=build('production',spu/'spu_interp.c')
for mode in ['normal','empty']:
    r=run(exe,mode);assert r.returncode==0,r.stdout+r.stderr
for mode,message in [('bad','continuation/status'),('plan','invalid plan'),('ambiguous','ambiguous guards')]:
    r=run(exe,mode)
    assert r.returncode==86 and '[spu-native-leaf] invalid execution: '+message in r.stderr,r.stdout+r.stderr
for variable in ['SPU_LS_LOWREAD','SPU_SMC_WATCH','SPU_PCHIST','YDKJ_SPU_TRACE','YDKJ_CRI_GATE1TRACE','SPU_STEPCAP']:
    r=run(exe,'instrumented',{variable:'100'})
    assert r.returncode==0,r.stdout+r.stderr

source=(spu/'spu_interp.c').read_text()
for name,before,after in [
    ('missing-seam','if (native_count && ctx->pc<0x40000','if (0 && ctx->pc<0x40000'),
    ('stop-bypass','(stop>=leaf->begin && stop<leaf->end)','0'),
    ('budget-bypass','if (plan.instructions>remaining) continue;','/* omitted budget admission */'),
]:
    assert source.count(before)==1,name
    path=out/(name+'.c');path.write_text(source.replace(before,after))
    r=run(build(name,path))
    assert r.returncode==1 and 'FAIL real host execution count' in r.stderr,r.stdout+r.stderr
print('PASS actual interpreter native-leaf state/stop/budget/guard/FP/thread tests; three unsafe dispatch mutants rejected')
