import os, fcntl, time, sys, json

def crc(data):
    c=255
    for x in data:
        c ^= x
        for _ in range(8):
            c=((c<<1)^0x31)&255 if c&128 else (c<<1)&255
    return c

bus=sys.argv[1] if len(sys.argv)>1 else '/dev/i2c-0'
fd=os.open(bus,os.O_RDWR)
fcntl.ioctl(fd,0x0703,0x44)
try:
    os.write(fd,b'\x89'); time.sleep(.01)
    r=os.read(fd,6)
    assert len(r)==6 and crc(r[:2])==r[2] and crc(r[3:5])==r[5], 'serial CRC failed'
    print('serial=%08x' % ((int.from_bytes(r[:2],'big')<<16)|int.from_bytes(r[3:5],'big')),flush=True)
    for i in range(10):
        os.write(fd,b'\xfd'); time.sleep(.012)
        r=os.read(fd,6)
        assert len(r)==6 and crc(r[:2])==r[2] and crc(r[3:5])==r[5], 'measurement CRC failed'
        t=-45+175*int.from_bytes(r[:2],'big')/65535
        h=max(0,min(100,-6+125*int.from_bytes(r[3:5],'big')/65535))
        print(json.dumps({'sample':i+1,'temperature_C':round(t,3),'humidity_percent':round(h,3),'crc_ok':True}),flush=True)
        time.sleep(1)
finally:
    os.close(fd)
