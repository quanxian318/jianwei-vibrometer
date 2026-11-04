import os,json,hashlib,shutil,subprocess,time
app='/root/vibmon_cam';stage='/root/printer_uart_debug_20261102'
def read(p):
    with open(p) as f:return f.read()
def write(p,s):
    with open(p,'w') as f:f.write(s)
def sha(p):
    with open(p,'rb') as f:return hashlib.sha256(f.read()).hexdigest()
manifest=json.loads(read(stage+'/manifest.json'))
for name in ['vibmond','printer_uart.bit.bin']:
    assert sha(stage+'/'+name)==manifest[name],name+' hash mismatch'
assert sha(app+'/vibmond')=='9945352a552748c71592cfac85092bc05808bc186b9baf5c9c454a6141591af7','Device app changed'
if subprocess.call(['pgrep','-x','vibmond'],stdout=subprocess.DEVNULL)==0:
    status=json.loads(subprocess.check_output([app+'/vibmonctl','-s','/tmp/vibmon.sock','status']).decode())['data']
    assert status['measurement']['state'] in ['preview','idle','result','done','stopped'],'Measurement active'
startup=read(app+'/start_an5642.sh')
assert 'BIT=$DIR/nano_uart.bit.bin' in startup
oldcfg='/root/nano_uart_debug_20261101/nano_board.json'
assert oldcfg in startup
cfg=json.loads(read(oldcfg))
assert cfg['hardware']['devices']['uart_nano']=='/dev/ttyPS1'
cfg.setdefault('printer',{})['enabled']=True
cfg['hardware']['devices']['uart_printer']='axi16550:0x44050000'
newcfg=stage+'/board.json'
backup=stage+'/before_'+str(int(time.monotonic()));os.mkdir(backup)
for n in ['vibmond','start_an5642.sh']:shutil.copy2(app+'/'+n,backup+'/'+n)
shutil.copy2(oldcfg,backup+'/nano_board.json')
write(stage+'/last_backup.txt',backup)
subprocess.call(['pkill','-TERM','-x','vibmond'])
for i in range(20):
    if subprocess.call(['pgrep','-x','vibmond'],stdout=subprocess.DEVNULL)!=0:break
    time.sleep(1)
else:raise RuntimeError('UI stop timeout')
try:
    shutil.copy2(stage+'/printer_uart.bit.bin',app+'/printer_uart.bit.bin')
    subprocess.check_call(['python3',app+'/prepare_an5642_runtime.py',app+'/printer_uart.bit.bin'])
    subprocess.check_call(['python3',stage+'/loopback.py'])
    write(newcfg,json.dumps(cfg,indent=2))
    startup=startup.replace('BIT=$DIR/nano_uart.bit.bin','BIT=$DIR/printer_uart.bit.bin').replace(oldcfg,newcfg)
    write(app+'/start_an5642.sh',startup)
    shutil.copy2(stage+'/vibmond',app+'/vibmond.new');os.chmod(app+'/vibmond.new',0o755)
    os.rename(app+'/vibmond.new',app+'/vibmond')
except Exception:
    shutil.copy2(backup+'/vibmond',app+'/vibmond')
    shutil.copy2(backup+'/start_an5642.sh',app+'/start_an5642.sh')
    raise
finally:
    env=dict(os.environ);env.pop('SSH_ORIGINAL_COMMAND',None)
    subprocess.check_call(['/bin/sh',app+'/start_ui_from_pc.sh','start'],env=env)
print(json.dumps({'backup':backup,'printer_uart':'AXI16550 0x44050000','nano_uart':'PS UART0 /dev/ttyPS1 unchanged','firmware':'Nano not flashed'}),flush=True)
