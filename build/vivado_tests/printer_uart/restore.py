import os,time,shutil,subprocess
stage='/root/printer_uart_debug_20261102';app='/root/vibmon_cam'
with open(stage+'/last_backup.txt') as f:backup=f.read().strip()
assert backup.startswith(stage+'/before_') and os.path.isdir(backup)
for n in ['vibmond','start_an5642.sh']:assert os.path.isfile(backup+'/'+n)
subprocess.call(['pkill','-TERM','-x','vibmond'])
for i in range(20):
    if subprocess.call(['pgrep','-x','vibmond'],stdout=subprocess.DEVNULL)!=0:break
    time.sleep(1)
else:raise RuntimeError('UI stop timeout')
for n in ['vibmond','start_an5642.sh']:shutil.copy2(backup+'/'+n,app+'/'+n)
env=dict(os.environ);env.pop('SSH_ORIGINAL_COMMAND',None)
subprocess.check_call(['/bin/sh',app+'/start_ui_from_pc.sh','start'],env=env)
print('Restored '+backup)
