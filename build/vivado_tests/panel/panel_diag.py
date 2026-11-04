from __future__ import print_function
import os,mmap,struct,time,json,sys,ctypes

fd=os.open('/dev/mem',os.O_RDWR|os.O_SYNC)
m=mmap.mmap(fd,4096,offset=0x44070000)
def read(o):
    r=ctypes.c_uint32.from_buffer(m,o)
    value=r.value
    del r
    return value
def write(o,v):
    # AXI GPIO is a 32-bit device register. A bytewise pack may transiently
    # change the commit bit before the new RGB word has arrived.
    r=ctypes.c_uint32.from_buffer(m,o)
    r.value=v
    if o==0 and r.value!=v:raise RuntimeError('GPIO command readback mismatch')
    del r
def snapshot():
    v=read(8)
    if v>>24!=0xa5:raise RuntimeError('Panel probe signature missing: %08x'%v)
    c=(v>>8)&65535
    if c>=32768:c-=65536
    return {'time':round(time.monotonic(),3),'raw':'%08x'%v,'k1_pressed':not bool(v&1),
            'encoder_pressed':not bool(v&2),'a':bool(v&4),'b':bool(v&8),
            'busy':bool(v&16),'encoder_count':c}
toggle=read(0)&0x80000000
def colour(rgb,index=7):
    global toggle
    toggle^=0x80000000
    write(0,toggle|(index<<24)|rgb)
    time.sleep(.01)
def emit(v):print(json.dumps(v));sys.stdout.flush()
mode=sys.argv[1] if len(sys.argv)>1 else 'status'
emit(snapshot())
if mode=='lights':
    write(4,0)
    try:
        colour(0)
        for name,rgb in [('red',0x100000),('green',0x001000),('blue',0x000010)]:
            emit({'phase':name,'rgb':'%06x'%rgb});colour(rgb);time.sleep(3);colour(0);time.sleep(1)
        for i in range(5):
            emit({'phase':'individual','led':i+1});colour(0);colour(0x080808,i);time.sleep(1.5)
    finally:colour(0);emit({'phase':'finished_black'})
elif mode=='pulse':
    write(4,0)
    end=time.monotonic()+(float(sys.argv[2]) if len(sys.argv)>2 else 120)
    sent=0
    emit({'phase':'continuous_low_brightness_data','duration_s':round(end-time.monotonic())})
    try:
        while time.monotonic()<end:
            toggle^=0x80000000
            write(0,toggle|0x07080808)
            sent+=1
            time.sleep(.001)
    finally:
        colour(0);emit({'phase':'finished_black','commands':sent})
elif mode=='lights_off':
    write(4,0);colour(0);emit({'phase':'finished_black'})
elif mode=='watch':
    end=time.monotonic()+(float(sys.argv[2]) if len(sys.argv)>2 else 30)
    previous=None
    while time.monotonic()<end:
        s=snapshot();key=(s['k1_pressed'],s['encoder_pressed'],s['a'],s['b'],s['encoder_count'])
        if key!=previous:emit(s);previous=key
        time.sleep(.005)
m.close();os.close(fd)
