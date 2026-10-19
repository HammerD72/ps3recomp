"""Build actual SHUFB callers at baseline ISA, with checked dispatch/fallback.

Optional --require-simd-code asserts SSSE3 is present only in the dispatched
object. Use it on an SSSE3 host; the value tests themselves run on any host.
"""
import argparse
import json
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--require-simd-code', action='store_true')
    ap.add_argument('--benchmark', action='store_true')
    args = ap.parse_args()
    out = ROOT / 'temp/checks' / ('shufb-' + str(time.time_ns()))
    out.mkdir(parents=True)
    compiler = os.environ.get('MV_CLANG') or os.environ.get('CC') or shutil.which('clang-cl' if os.name == 'nt' else 'cc')
    assert compiler, 'Set MV_CLANG/CC or install a C compiler'
    version = subprocess.run([compiler, '--version' if os.name != 'nt' else '/clang:--version'], capture_output=True, text=True)
    (out/'compiler-version.log').write_text(version.stdout+version.stderr)
    binaries = {}
    for name, defines in [('dispatch', []), ('scalar', ['SPU_SHUFB_FORCE_SCALAR']), ('cpp', [])]:
        exe = out / (name + ('.exe' if os.name == 'nt' else ''))
        obj = out / (name + ('.obj' if os.name == 'nt' else '.o'))
        source = ROOT / 'runtime/spu/tests/test_spu_shufb_dispatch.c'
        if os.name == 'nt':
            compile_cmd = [compiler, '/nologo', '/c', '/O1', '/UNDEBUG', '/I'+str(ROOT/'runtime/spu'), str(source), '/Fo'+str(obj)]
            compile_cmd += ['/D'+x for x in defines]
            if name == 'cpp': compile_cmd += ['/TP']
            link_cmd = [compiler, '/nologo', str(obj), '/Fe'+str(exe)]
        else:
            compile_cmd = [compiler, '-std=c11', '-O1', '-UNDEBUG', '-I'+str(ROOT/'runtime/spu'), '-c', str(source), '-o', str(obj)]
            compile_cmd += ['-D'+x for x in defines]
            if platform.machine().lower() in ('x86_64', 'amd64'):
                # Some distributions default to x86-64-v3. Verify the actual
                # baseline contract rather than inheriting the runner's ISA.
                compile_cmd += ['-march=x86-64', '-mtune=generic']
            if name == 'cpp': compile_cmd[1:1] = ['-x', 'c++', '-std=c++17']
            if name == 'cpp': compile_cmd.remove('-std=c11')
            link_cmd = [compiler, str(obj), '-lm', '-o', str(exe)]
        for index, cmd in enumerate([compile_cmd, link_cmd]):
            result = subprocess.run(cmd, capture_output=True, text=True)
            (out/f'{name}-{index}.log').write_text(result.stdout+result.stderr)
            assert result.returncode == 0, result.stdout+result.stderr
        result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
        print(name, result.stdout.strip(), flush=True)
        binaries[name] = exe
        if args.require_simd_code:
            assert f'simd={int(name != "scalar")}' in result.stdout, result.stdout
            disassembler = shutil.which('llvm-objdump') or shutil.which('objdump')
            assert disassembler, 'Install llvm-objdump or objdump'
            assembly = subprocess.check_output([disassembler, '-d', str(obj)], text=True)
            (out/f'{name}.asm').write_text(assembly)
            has_shuffle = bool(re.search(r'\bpshufb\b', assembly))
            assert has_shuffle == (name != 'scalar'), f'{name}: expected baseline checked SIMD code={name != "scalar"}, found={has_shuffle}\ncompiler={compile_cmd}\ndisassembler={disassembler}\n{assembly}'
    if args.benchmark:
        rows = []
        for pair in range(5):
            pair_rows = []
            for name in ['scalar', 'dispatch']:
                result = subprocess.check_output([str(binaries[name]), '--bench'], text=True)
                print(pair, name, result.strip(), flush=True)
                match = re.search(r'seconds=([\d.]+) checksum=(\S+)', result)
                assert match, result
                pair_rows.append(dict(pair=pair, name=name, seconds=float(match[1]), checksum=match[2]))
            assert pair_rows[0]['checksum'] == pair_rows[1]['checksum'], pair_rows
            rows += pair_rows
        (out/'benchmark.json').write_text(json.dumps(rows, indent=2))
    print('Evidence:', out)

if __name__ == '__main__':
    main()
