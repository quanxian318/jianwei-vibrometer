import os,mmap,ctypes,time,json
fd=os.open('/dev/mem',os.O_RDWR|os.O_SYNC)
m=mmap.mmap(fd,8192,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=0x44050000)
def rd(r):return ctypes.c_uint32.from_buffer(m,0x1000+4*r).value&255
def wr(r,v):ctypes.c_uint32.from_buffer(m,0x1000+4*r).value=v
try:
    wr(3,3);wr(1,0)
    old=rd(7)
    for v in [0x5a,0xa5]:
        wr(7,v)
        assert rd(7)==v,'UART scratch register failure'
    wr(7,old)
    wr(3,0x83);wr(0,54);wr(1,0);wr(3,3)
    wr(4,0x13);time.sleep(.02);wr(2,7);time.sleep(.02)
    print(json.dumps({'before_lsr':rd(5),'iir':rd(2),'mcr':rd(4)}),flush=True)
    tx=b'AX-PRN\x55\xaa';rx=bytearray()
    for v in tx:
        end=time.monotonic()+1
        while not rd(5)&0x20:
            assert time.monotonic()<end,'TX timeout'
            time.sleep(.001)
        wr(0,v)
        time.sleep(.002)
        while not rd(5)&1:
            assert time.monotonic()<end,'RX timeout'
            time.sleep(.001)
        rx.append(rd(0))
    print(json.dumps({'tx':list(tx),'rx':list(rx),'lcr':rd(3),'mcr':rd(4),'lsr':rd(5)}),flush=True)
    assert bytes(rx)==tx,'Loopback data mismatch'
    print(json.dumps({'uart_loopback':'passed','bytes':len(tx),'lsr':hex(rd(5)),'msr_loopback':hex(rd(6))}),flush=True)
finally:
    wr(4,3);wr(2,7);wr(1,0)
    m.close();os.close(fd)
