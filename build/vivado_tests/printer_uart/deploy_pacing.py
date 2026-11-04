import os,time,json,shutil,hashlib,subprocess
app='/root/vibmon_cam';stage='/root/printer_uart_debug_20261102';cfgpath=stage+'/board.json'
def sha(p):
    with open(p,'rb') as f:return hashlib.sha256(f.read()).hexdigest()
assert sha(stage+'/vibmond_pacing')=='ec2b113243046bbaf470affe4d4521b44e31878cf4fa464a000ecbfc061d0d61'
assert sha(app+'/vibmond')=='78c13b4cc2382f02d8e3fae223e5e3127f44afecfd79c2f0b70eebb8a56d2e83'
s=json.loads(subprocess.check_output([app+'/vibmonctl','-s','/tmp/vibmon.sock','status']).decode())['data']
assert s['measurement']['state']!='capturing' and s['printer']=='idle','Capture or print active'
backup=stage+'/before_pacing_'+str(int(time.monotonic()));os.mkdir(backup)
for src,name in [(app+'/vibmond','vibmond'),(cfgpath,'board.json'),(app+'/ui_from_pc.log','ui.log')]:shutil.copy2(src,backup+'/'+name)
with open(cfgpath) as f:cfg=json.load(f)
cfg['printer']['raster_pause_ms']=120
cfg['printer']['raster_strip_rows']=24
subprocess.call(['pkill','-TERM','-x','vibmond'])
for i in range(20):
    if subprocess.call(['pgrep','-x','vibmond'],stdout=subprocess.DEVNULL)!=0:break
    time.sleep(1)
else:raise RuntimeError('UI stop timeout')
try:
    shutil.copy2(stage+'/vibmond_pacing',app+'/vibmond.new');os.chmod(app+'/vibmond.new',0o755)
    os.rename(app+'/vibmond.new',app+'/vibmond')
    with open(cfgpath+'.new','w') as f:json.dump(cfg,f,indent=2)
    os.rename(cfgpath+'.new',cfgpath)
except Exception:
    shutil.copy2(backup+'/vibmond',app+'/vibmond');shutil.copy2(backup+'/board.json',cfgpath)
    raise
finally:
    with open(app+'/ui_from_pc.log','wb') as log:
        subprocess.Popen([app+'/vibmond','-c',cfgpath],cwd=app,stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
print(json.dumps({'backup':backup,'strip_rows':24,'pause_ms':120}),flush=True)
