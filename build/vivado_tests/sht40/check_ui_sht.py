import socket,json,time
samples=[]
for i in range(10):
    s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect('/tmp/vibmon.sock')
    s.sendall(b'{"cmd":"status"}\n')
    buf=b''
    while b'\n' not in buf:
        chunk=s.recv(65536)
        if not chunk: break
        buf+=chunk
    s.close()
    r=json.loads(buf.decode())
    assert r['ok']
    d=r['data']; e=d['sensors']
    assert e['temperature_c'] is not None and e['humidity_percent'] is not None
    assert d['tca_ok'] and d['ina_ok'] and d['rtc_ok']
    sample={'sample':i+1,'temperature_C':e['temperature_c'],'humidity_percent':e['humidity_percent'],'management_ok':True}
    samples.append(sample)
    print(json.dumps(sample),flush=True)
    time.sleep(1)
with open('/root/sht40_debug_20261029/ui_sht_samples.json','w') as f: json.dump(samples,f,indent=2)
