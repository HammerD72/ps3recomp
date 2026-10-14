"""Authored live-process regression for the x64 hardware write observer."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main(mode='writes'):
    if os.name != 'nt':
        print('SKIP: Windows debug API fixture'); return
    from windows_data_watch import process_creation_time
    out = ROOT / 'temp/checks' / ('data-watch-'+mode+'-'+str(time.time_ns()))
    out.mkdir(parents=True)
    source = out / 'writer.c'; exe = out / 'writer.exe'
    source.write_text(r'''
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
__declspec(align(16)) volatile uint64_t cells[2];
static DWORD WINAPI worker(void *unused) {
    (void)unused; cells[0] = 2; cells[1] = 4; return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    printf("%llu %llu\n", (unsigned long long)(uintptr_t)&cells[0],
           (unsigned long long)(uintptr_t)&cells[1]); fflush(stdout);
    for (int n = 0; GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES; ++n) {
        if (n > 2000) return 3; Sleep(10);
    }
    cells[0] = 1; cells[1] = 3;
    HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!thread) return 4;
    WaitForSingleObject(thread, INFINITE); CloseHandle(thread);
    Sleep(200); cells[0] = 5; cells[1] = 6; /* must run after detach */
    printf("FINAL %llu %llu\n", (unsigned long long)cells[0], (unsigned long long)cells[1]);
    return cells[0] != 5 || cells[1] != 6;
}
''')
    command = [os.environ.get('MV_CLANG', 'clang-cl'), '/nologo', '/O1', '/Zi',
               str(source), '/Fe'+str(exe), '/Fo'+str(out/'writer.obj'),
               '/link', 'kernel32.lib', '/PDB:'+str(out/'writer.pdb')]
    build = subprocess.run(command, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out/'build.log').write_text(build.stdout)
    assert build.returncode == 0, build.stdout
    go = out / 'go.txt'
    child = subprocess.Popen([str(exe), str(go)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    observer = None
    try:
        addresses = [int(x) for x in child.stdout.readline().split()]
        assert len(addresses) == 2
        owner = dict(pid=child.pid, create_time=process_creation_time(int(child._handle)),
                     exe=str(exe), exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
        manifest = out / 'owner.json'; manifest.write_text(json.dumps(owner))
        watch = out / 'watch'
        cmd = [sys.executable, str(ROOT/'tools/windows_data_watch.py'), '--owner', str(manifest),
               '--out', str(watch), '--events', '4' if mode=='writes' else '100',
               '--seconds', '15' if mode=='writes' else '.2', '--dump-nonzero', '1']
        for a in addresses: cmd.extend(['--address', hex(a)])
        observer = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic()+10
        while not (watch/'ready.json').exists():
            if observer.poll() is not None: raise AssertionError(observer.communicate())
            assert time.monotonic() < deadline, 'Debugger did not finish attachment'
            time.sleep(.01)
        if mode == 'writes': go.write_text('authored writes')
        stdout, stderr = observer.communicate(timeout=20)
        assert observer.returncode == 0, (stdout, stderr)
        result = json.loads((watch/'final.json').read_text())
        assert result['hits'] == (4 if mode=='writes' else 0) and not result['cleanup_errors'], result
        events = [json.loads(line) for line in (watch/'events.jsonl').read_text().splitlines()]
        writes = [e for e in events if e['kind'] == 'write']
        if mode=='writes':
            assert len({e['tid'] for e in writes}) == 2, 'Newly-created writer was missed'
        for e, expected in zip(writes, [(0,1), (1,3), (0,2), (1,4)]):
            index, value = expected
            assert e['mask'] & (1 << index)
            assert int.from_bytes(bytes.fromhex(e['values'][index]), 'little') == value, e
        if mode=='writes': assert any(e.get('dump') for e in writes)
        else:
            assert result['reason']=='duration limit', result
            go.write_text('writes after bounded no-hit detach')
        final, err = child.communicate(timeout=10)
        assert child.returncode == 0 and 'FINAL 5 6' in final, (child.returncode, final, err)
        (out/'result.json').write_text(json.dumps(dict(passed=True, writer_sha256=owner['exe_sha256'],
            tool_sha256=hashlib.sha256((ROOT/'tools/windows_data_watch.py').read_bytes()).hexdigest(),
            hits=result['hits'], mode=mode, detached_writes='5 6'), indent=2))
        print('PASS: '+mode+'; '+str(out))
    finally:
        if observer and observer.poll() is None:
            # Observer is bounded and restores debug registers in its normal finally.
            observer.communicate(timeout=25)
        if child.poll() is None:
            child.terminate(); child.communicate(timeout=5)


if __name__ == '__main__':
    for mode in ('writes', 'deadline'): main(mode)
