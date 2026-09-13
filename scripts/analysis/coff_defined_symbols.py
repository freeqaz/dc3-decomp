import struct,sys
def syms(path):
    d=open(path,'rb').read()
    mach,nsec,ts,psym,nsym,osz,ch=struct.unpack_from('<HHIIIHH',d,0)
    out=[]
    st=psym+nsym*18
    i=0
    while i<nsym:
        rec=d[psym+i*18:psym+i*18+18]
        z=struct.unpack_from('<I',rec,0)[0]
        if z==0:
            off=struct.unpack_from('<I',rec,4)[0]
            e=d.index(b'\0',st+off); name=d[st+off:e].decode('latin1')
        else:
            name=rec[:8].rstrip(b'\0').decode('latin1')
        val,secnum,typ,sclass,naux=struct.unpack_from('<IhHBB',rec,8)
        out.append((name,secnum,sclass,typ))
        i+=1+naux
    return out
for name,sec,sc,typ in syms(sys.argv[1]):
    if sc in (2,3) and sec>0:  # external / static, defined
        print(name)
