# Disassembler for Championship Boxing overlays. Run it in a folder holding the BOXING.* files.
# Usage: python3 cbdis.py <GYM|FGT|FG2|OPT|OVR>  -> <name>.asm, cs_<name>.bin, ds_<name>.bin
import sys, capstone, re
d=lambda f:open(f'BOXING.{f}','rb').read()
OVL={'GYM':('OV2','GYM','MSG'),'FGT':('OV3','FGT','MS2'),'FG2':('OV4','FG2','FG3'),'OPT':('OV5','OPT','OP1')}
def build(name):
    cs=bytearray(0x10000); ds=bytearray(0x10000)
    ovr=d('OVR')[0x200:]
    cs[0x100:0x100+len(ovr)]=ovr
    dd=ovr[0x75A0:]; ds[0:len(dd)]=dd
    rng=(0x100,0x76A0)
    if name in OVL:
        x=b''.join(d(f) for f in OVL[name])
        ca,cz,da,dz=[x[i]|x[i+1]<<8 for i in (0,2,4,6)]
        cs[ca:ca+cz]=x[16:16+cz]
        ds[da:da+dz]=x[16+cz:16+cz+dz]
        rng=(ca,ca+cz)
    return cs,ds,rng
if __name__=='__main__':
    name=sys.argv[1]
    cs,ds,(start,end)=build(name)
    if len(sys.argv)>3: start,end=int(sys.argv[2],16),int(sys.argv[3],16)
    open(f'cs_{name}.bin','wb').write(cs); open(f'ds_{name}.bin','wb').write(ds)
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_16)
    def strat(a):
        s=b''
        while a<0xffff and (32<=ds[a]<127 or ds[a] in (8,10,13)): s+=bytes([ds[a]]);a+=1
        return s.decode() if len(s.strip(b'\x08'))>=2 and ds[a]==0 else None
    pc=start; out=[]
    while pc<end:
        ins=next(md.disasm(bytes(cs[pc:pc+16]),pc),None)
        if ins is None:
            out.append(f'{pc:04X}  db {cs[pc]:02X}'); pc+=1; continue
        extra=''
        if ins.mnemonic in('lea','mov','push'):
            for m in re.findall(r'0x([0-9a-f]{3,4})',ins.op_str):
                s=strat(int(m,16))
                if s: extra+='   ; "'+s[:60].replace('\n','\\n').replace('\r','\\r').replace('\x08','')+'"'
        out.append(f'{pc:04X}  {ins.mnemonic} {ins.op_str}{extra}')
        pc+=ins.size
        if ins.mnemonic=='call' and ins.op_str.startswith('0x'):
            t=int(ins.op_str,16)
            if t==0x6cab: out.append(f'      ; ^^^ FUNC locals={cs[pc]}'); pc+=1
            elif t==0x6cbe: out.append(f'      ; ^^^ FUNC locals={cs[pc]|cs[pc+1]<<8}'); pc+=2
            elif t==0x6ca6: out.append('      ; ^^^ FUNC locals=0')
    open(f'{name}.asm','w').write('\n'.join(out)+'\n')
    print(name,hex(start),hex(end),len(out))
