#!/usr/bin/env python3
"""Execute actual generated C++ nonlocal returns and a historical red control."""
import os, pathlib, shlex, subprocess, sys, tempfile
from types import SimpleNamespace
import ppu_lifter as lifter

ROOT = pathlib.Path(__file__).resolve().parents[1]

def source(enabled, alias=False, foreign=False):
    generator = lifter.PPULifter()
    generator.cpp_nonlocal_jumps = enabled
    generator.name_map = {0x2000: '.setjmp', 0x3000: '_longjmp'}
    bodies = {
        0x1000: ['ctx->gpr[1] -= 128;', '++level;',
                 'ctx->lr = 0x00001010; func_00002000(ctx);',
                 'if (!ctx->gpr[3]) { if (level < limit) func_00001000(ctx); else func_00003000(ctx); ++wrong; }',
                 '++resumed;', '--level;', 'ctx->gpr[1] += 128;', 'return;'],
        0x2000: ['if (level == 1) { saved_sp = ctx->gpr[1]; saved_pc = ctx->lr; }',
                 'ctx->gpr[3] = 0;', 'return;'],
        0x3000: ['ctx->gpr[1] = saved_sp;', 'ctx->lr = saved_pc;',
                 'ctx->gpr[3] = 7;', 'return;'],
    }
    if alias:
        bodies[0x1000][2] = 'ctx->lr = 0x00001010; func_00004000(ctx);'
        bodies[0x4000] = ['{ g_trampoline_fn = (void(*)(void*))func_00002000; return; }']
        # Use the real trampoline drain emitted after direct calls.
        bodies[0x1000][2] += ' DRAIN_TRAMPOLINE(ctx);'
    if foreign:
        bodies[0x1000][3] = 'throw ppu_nonlocal_jump{&other, 0x1010, ctx->gpr[1]};'
    functions = {a: SimpleNamespace(start_addr=a, name=f'func_{a:08X}',
                  body_lines=b, fallthrough_to=0) for a, b in bodies.items()}
    code = [lifter.HEADER_PREAMBLE,
            'extern "C" { PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*) = nullptr; }',
            'static unsigned level, limit, wrong, resumed; static uint64_t saved_sp, saved_pc;',
            'static ppu_context other;', lifter.SOURCE_PREAMBLE.replace('#include "ppu_recomp.h"', '')]
    code.extend(f'void func_{a:08X}(ppu_context*);' for a in functions)
    for function in functions.values():
        code.extend(generator._function_def_lines(function, functions, sorted(functions),
                    {a: i for i, a in enumerate(sorted(functions))}))
    if foreign:
        code.append(r'''
int main() {
    ppu_context ctx = {}; ctx.gpr[1] = 0x10000;
    try { func_00001000(&ctx); }
    catch (const ppu_nonlocal_jump& jump) {
        return jump.ctx == &other && jump.pc == 0x1010 && !resumed ? 0 : 1;
    }
    return 1;
}
''')
    else:
        code.append(r"""
#include <stdio.h>
int main() {
    unsigned failures = 0;
    for (unsigned depth : {1u, 4u}) {
        ppu_context ctx = {}; ctx.gpr[1] = 0x10000;
        level = wrong = resumed = 0; limit = depth;
        func_00001000(&ctx);
        if (wrong || resumed != 1 || level != depth - 1 || ctx.gpr[1] != 0x10000 || ctx.gpr[3] != 7) {
            printf("FAIL depth=%u wrong=%u resumed=%u level=%u sp=%llx\n", depth, wrong, resumed, level, (unsigned long long)ctx.gpr[1]);
            ++failures;
        }
    }
    printf("nonlocal checks=2 failures=%u\n", failures);
    return failures ? 1 : 0;
}
""")
    return '#include <initializer_list>\n' + '\n'.join(code)

def main():
    compiler = shlex.split(os.environ.get('CXX', 'clang++'))
    rejected = subprocess.run([sys.executable, str(ROOT/'tools/ppu_lifter.py'),
                              'unused-input', '--cpp-nonlocal-jumps'], capture_output=True, text=True)
    assert rejected.returncode == 2 and 'requires C++ output' in rejected.stderr, rejected.stderr
    for enabled, alias, foreign in ((False, False, False), (True, False, False),
                                    (True, True, False), (True, True, True)):
        with tempfile.TemporaryDirectory(prefix='ppu-cpp-nonlocal-') as directory:
            directory = pathlib.Path(directory)
            cpp = directory / 'generated.cpp'; exe = directory / 'test.exe'
            cpp.write_text(source(enabled, alias, foreign), encoding='utf-8')
            subprocess.run(compiler + ['-std=c++17', '-O1', '-UNDEBUG', '-I'+str(ROOT/'include'),
                                      '-I'+str(ROOT/'runtime/ppu'), str(cpp), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            print(f'enabled={enabled} alias={alias} foreign={foreign}: ' + result.stdout)
            assert result.returncode == (0 if enabled else 1), (enabled, result.returncode, result.stderr)
    print('PASS generated nonlocal returns and historical negative control')

if __name__ == '__main__':
    main()
