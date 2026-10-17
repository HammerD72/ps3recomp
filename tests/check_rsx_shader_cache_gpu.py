import subprocess,sys
for hardware in [False,True]:
 for bypass in [False,True]:
  args=[sys.argv[1]]+(['--hardware']if hardware else[])+(['--no-cache']if bypass else[])
  p=subprocess.run(args,capture_output=True,text=True,timeout=40)
  print(p.stdout,end='');print(p.stderr,end='')
  assert p.returncode==(1 if bypass else 0),(args,p.returncode)
  assert 'checks=10 failures=%d'%(1 if bypass else 0) in p.stdout
