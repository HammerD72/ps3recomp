import subprocess,sys
for legacy in [False,True]:
 p=subprocess.run([sys.argv[1]]+(['--fixed96'] if legacy else []),capture_output=True,text=True,timeout=15)
 print(p.stdout,end='');print(p.stderr,end='')
 assert p.returncode==(1 if legacy else 0)
 assert 'checks=9 failures=%d'%(1 if legacy else 0) in p.stdout
