import pathlib,json,hashlib,os,shutil,subprocess
root=pathlib.Path('/root/vibmon_cam')
stage=pathlib.Path('/root/sht40_debug_20261029')
newbit=stage/'sensor.bit.bin'
with open(str(newbit),'rb') as f: assert hashlib.sha256(f.read()).hexdigest()=='1d172af4349fa896ee4c6c75e35ad0df249ca6889b39083ce2d5f576b7db2b57'
assert (stage/'runtime_before.tar.gz').is_file()
assert subprocess.call(['pgrep','-x','vibmond'],stdout=subprocess.DEVNULL)!=0,'stop UI before replacing runtime'
cfgpath=root/'an5642_board.json'
with open(str(cfgpath)) as f: cfg=json.load(f)
cfg.setdefault('hardware',{}).setdefault('devices',{})['i2c_sensors']='/dev/i2c-0'
cfg.setdefault('sensors',{}).setdefault('sht40',{})['enabled']=True
tmp=cfgpath.with_suffix('.json.sht40-new')
with open(str(tmp),'w') as f: f.write(json.dumps(cfg,indent=2)+'\n')
target=root/'an5642_stereo_sgm40_pdm.bit.bin'
shutil.copyfile(str(newbit),str(target)+'.sht40-new')
os.replace(str(target)+'.sht40-new',str(target))
os.replace(str(tmp),str(cfgpath))
os.sync()
print('SHT40 runtime enabled; application binary unchanged')
