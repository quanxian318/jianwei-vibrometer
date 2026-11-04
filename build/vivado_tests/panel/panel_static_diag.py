import os,mmap,ctypes,time,json,sys
fd=os.open('/dev/mem',os.O_RDWR|os.O_SYNC)
m=mmap.mmap(fd,4096,offset=0x44070000)
def rd(o):
 r=ctypes.c_uint32.from_buffer(m,o);v=r.value;del r;return v
def wr(v):
 r=ctypes.c_uint32.from_buffer(m,0);r.value=v
 if r.value!=v:raise RuntimeError('Command readback mismatch')
 del r
def emit(v):print(json.dumps(v));sys.stdout.flush()
def status():
 v=rd(8)
 if v>>24!=0xa7:raise RuntimeError('Static diagnostic firmware not loaded: %08x'%v)
 return {'t':round(time.monotonic(),3),'raw':'%08x'%v,'frames':(v>>8)&65535,
         'force':bool(v&64),'output_high':bool(v&32),'busy':bool(v&16),
         'k1_pressed':not bool(v&1),'encoder_pressed':not bool(v&2),'ab':(v>>2)&3}
toggle=rd(0)&0x80000000
def colour(rgb,index=7):
 global toggle
 toggle^=0x80000000;wr(toggle|(index<<24)|rgb);time.sleep(.01)
def static(high):
 wr(toggle|(1<<29)|((1<<30) if high else 0));time.sleep(.01)
mode=sys.argv[1] if len(sys.argv)>1 else 'status'
emit(status())
if mode in ('high','low'):
 try:
  static(mode=='high');emit(status())
  time.sleep(float(sys.argv[2]) if len(sys.argv)>2 else 90)
 finally:
  static(False);emit(status());colour(0)
elif mode=='off':colour(0);emit(status())
elif mode=='lights':
 raise RuntimeError('Static-only firmware: RGB test unavailable')
 try:
  colour(0)
  for name,rgb in [('red',0x100000),('green',0x001000),('blue',0x000010)]:
   colour(rgb);s=status();s['phase']=name;emit(s);time.sleep(5)
  colour(0)
 finally:colour(0);emit(status())
elif mode=='watch':
 end=time.monotonic()+(float(sys.argv[2]) if len(sys.argv)>2 else 60)
 previous=rd(8)&15;ab_prev=((previous>>2)&1)*2+((previous>>3)&1)
 count=0;errors=0
 positive={2,11,13,4};negative={1,7,14,8}
 while time.monotonic()<end:
  v=rd(8);raw=v&15;ab=((raw>>2)&1)*2+((raw>>3)&1)
  if ab!=ab_prev:
   transition=(ab_prev<<2)|ab
   if transition in positive:count+=1
   elif transition in negative:count-=1
   else:errors+=1
   ab_prev=ab
  if raw!=previous:
   s=status();s.update({'ps_encoder_count':count,'transition_errors':errors});emit(s);previous=raw
  time.sleep(.001)
m.close();os.close(fd)
