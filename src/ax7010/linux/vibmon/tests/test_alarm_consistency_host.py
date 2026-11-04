import sys,pathlib,time,json,tempfile
source=pathlib.Path('tests/test_app_host.py').read_text()
exec(source.split("with tempfile.TemporaryDirectory(prefix='vibmon-regression-')")[0])
with tempfile.TemporaryDirectory(prefix='vibmon-alarm-') as d:
 app=Daemon(d,extra=[{'alarm':{'binary_threshold':True,'warn_peak_to_peak':.000001,'alarm_peak_to_peak':.00001}}])
 try:
  assert app.ready()
  assert app.cmd('measure_start')['ok']
  last=app.result();assert last['valid'],last
  assert last['peak_to_peak']>.00001,last
  assert app.cmd('status')['data']['alarm_level']=='报警'
  ident=last['measurement_id'];record=pathlib.Path(d)/'records'/ident/'record.json'
  saved=record.read_bytes()
  assert json.loads(saved)["result"]["thresholds"]["binary_threshold"] is True
  assert app.cmd('set',alarm_pp=100)['ok']
  st=app.cmd('status')['data'];assert st['alarm_level']=='正常' and st['measurement']['last']['level']=='正常',st
  assert app.cmd('set',alarm_pp=.00001)['ok']
  assert app.cmd('status')['data']['alarm_level']=='报警'
  assert record.read_bytes()==saved,'historical record mutated'
  assert app.cmd('measure_start')['ok'];time.sleep(.15);assert app.cmd('measure_stop')['ok']
  for i in range(100):
   st=app.cmd('status')['data'];last=st['measurement'].get('last',{})
   if last.get('measurement_id')!=ident:break
   time.sleep(.05)
  assert not last['valid'],last
  assert st['alarm_level']=='未判级',st
  assert last['peak_to_peak'] is None,last
  assert app.cmd('set',alarm_pp=.00002)['ok']
  assert app.cmd('status')['data']['alarm_level']=='未判级'
  print('alarm application: all checks passed',flush=True)
 finally:app.close()
