# Reads a cbdis.py listing and prints pseudo-C. Usage: python3 lift.py X.asm names.txt ds_X.bin
# Lift the stack-machine output of Championship Boxing's compiler into pseudo-C.
import sys,re
lines=[l for l in open(sys.argv[1]).read().split('\n') if l.strip()]
names={}
if len(sys.argv)>2:
    for l in open(sys.argv[2]):
        l=l.split('#')[0].strip()
        if l: k,v=l.split(None,1); names[int(k,16)]=v.strip()
ins=[];targets=set()
for l in lines:
    if l.startswith('      ;'):
        ins.append((None,l.strip()));continue
    a=int(l[:4],16); t=l[6:]
    cmt=''
    if ';' in t: t,cmt=t.split(';',1); cmt=cmt.strip()
    ins.append((a,t.strip(),cmt))
    m=re.match(r'j\w+ 0x([0-9a-f]+)',t.strip())
    if m: targets.add(int(m.group(1),16))
def gname(addr):
    if addr in names: return names[addr]
    return f'g{addr:04X}'
def fname(a):
    return names.get(a, f'f{a:04X}')
def memexpr(op,regs):
    # op like 'word ptr [bp - 2]' or 'word ptr [0x818e]' or 'word ptr [si]' or 'byte ptr [si]'
    m=re.search(r'\[(.*)\]',op); inner=m.group(1).replace(' ','')
    byte='byte' in op
    if re.fullmatch(r'0x[0-9a-f]+|\d+',inner): e=gname(int(inner,0))
    elif inner.startswith('bp'):
        off=int(inner[2:].replace('+',''),0) if inner!='bp' else 0
        e=f'arg{(off-4)//2}' if off>0 else f'loc{-off}'
    elif inner in ('si','di','bx'):
        e=deref(regs.get(inner,inner))
    elif re.fullmatch(r'(si|di|bx)\+0x[0-9a-f]+|(si|di|bx)\+\d+',inner):
        r,o=inner.split('+'); e=deref(f'({regs.get(r,r)}+{int(o,0)})')
    else: return None
    return ('(byte)' if byte else '')+e
def deref(p):
    m=re.fullmatch(r'&(\w+)',p)
    if m: return m.group(1)
    m=re.fullmatch(r'\(&(\w+) \+ (.+)\)',p)
    if m: return f'{m.group(1)}[{m.group(2)}]'
    return f'*({p})'
def val(op,regs):
    op=op.strip()
    if op in ('ax','si','cx','dx','bx','di','es','ds','cs','bp','sp'): return regs.get(op,op)
    if op in ('al','cl','dl','ah'): return regs.get(op,op)
    if 'ptr' in op or op.startswith('['):
        e=memexpr(op,regs); return e
    if re.fullmatch(r'-?0x[0-9a-f]+|-?\d+',op):
        v=int(op,0); return str(v if v<0x8000 else v-0x10000) if v<0x100 or v>0xff00 else hex(v)
    return None
out=[];regs={};stack=[];cmp=None
def emit(s): out.append('    '+s)
def simplify(e):
    return e
CC={'je':'==','jne':'!=','jl':'<','jg':'>','jle':'<=','jge':'>=','jb':'<u','ja':'>u','jbe':'<=u','jae':'>=u','jc':'<u','jnc':'>=u'}
BIN={'add':'+','sub':'-','and':'&','or':'|','xor':'^','imul':'*'}
for it in ins:
    if it[0] is None:
        if 'FUNC' in it[1]:
            out.append(''); out.append(f'// ---- function {fname(prev)} ({it[1].split("locals=")[1]} bytes locals)'); regs={};stack=[]
        continue
    a,t,cmt=it; prev=a
    if a in targets:
        regs={k:v for k,v in regs.items() if False}; out.append(f'L{a:04X}:')
    p=t.split(None,1); mn=p[0]; ops=[x.strip() for x in re.split(r',(?![^\[]*\])',p[1])] if len(p)>1 else []
    c=f'  /* "{cmt}" */' if cmt.startswith('"') else ''
    try:
        if mn=='call' and ops[0].startswith('0x'):
            tg=int(ops[0],16)
            if tg in (0x6cab,0x6cbe,0x6ca6):
                out.append(''); out.append(f'// ---- function {fname(a if False else lastlabel)}') if False else None
                continue
            # look ahead for stack cleanup
            j=ins.index(it); n=0
            nx=ins[j+1]
            if nx[0] and nx[1].startswith('add sp,'): n=int(nx[1].split(',')[1],0)//2
            elif nx[0] and nx[1]=='inc sp':
                n=1
            args=stack[len(stack)-n:] if n else []
            if n: del stack[len(stack)-n:]
            s=f'{fname(tg)}({", ".join(reversed(args))})'
            regs={'ax':s}; pending_call=s
            emit(s+';'+c) if False else None
            out.append(('    __CALL__',s))
            skip=('add' if n>1 or (nx[0] and nx[1].startswith('add sp')) else ('inc' if n==1 else None))
            continue
        if t=='inc sp':
            if out and isinstance(out[-1],tuple): 
                if getattr(sys,'_inc',0)==0: sys._inc=1; continue
                else: sys._inc=0; continue
            sys._inc=0
            continue
        if t.startswith('add sp,') and out and isinstance(out[-1],tuple): continue
        # flush pending call if its result unused
        if out and isinstance(out[-1],tuple):
            s=out.pop()[1]; 
            if mn=='mov' and ops[1]=='ax' or mn in('push','or','cmp') and 'ax' in ops: regs['ax']=s
            else: emit(s+';'); regs.pop('ax',None)
        if mn=='mov' and len(ops)==2:
            d,sv=ops
            v=val(sv,regs)
            if d in ('ax','si','cx','dx','bx','di','al','cl','dl','ah','es') and v is not None:
                regs[d]=v; 
                if d=='al': regs['ax']=v
                continue
            if ('ptr' in d or d.startswith('[')) and v is not None:
                m=memexpr(d,regs); emit(f'{m} = {v};'+c); continue
        if mn=='lea':
            m=re.search(r'\[(.*)\]',ops[1]).group(1).replace(' ','')
            if re.fullmatch(r'0x[0-9a-f]+',m): regs[ops[0]]='&'+gname(int(m,16))+ (c and '') 
            elif m.startswith('bp'):
                off=int(m[2:],0); regs[ops[0]]='&'+(f'arg{(off-4)//2}' if off>0 else f'loc{-off}')
            else: raise Exception
            if c: regs[ops[0]]=regs[ops[0]]; emit(f'/* {ops[0]} = {regs[ops[0]]} {c.strip()} */') if cmt else None
            continue
        if mn=='push':
            v=val(ops[0],regs)
            if v is None: raise Exception
            stack.append(v); continue
        if mn=='pop':
            v=stack.pop() if stack else ops[0]
            regs[ops[0]]=v; continue
        if mn in('cbw','cwde'): continue
        if mn=='cdq': regs['dx']='sx(ax)'; continue
        if mn in BIN and len(ops)==2:
            d,sv=ops; v=val(sv,regs)
            if mn=='xor' and d==sv: 
                regs[d]='0'; continue
            if mn=='or' and d==sv:
                cmp=(regs.get(d,d),'0'); continue
            if d in ('ax','si','cx','dx','bx','di','al') and v is not None:
                regs[d]=f'({regs.get(d,d)} {BIN[mn]} {v})'; continue
            if 'ptr' in d:
                m=memexpr(d,regs); emit(f'{m} {BIN[mn]}= {v};'); 
                for k in list(regs):
                    if k in('ax',) and regs[k]==m: pass
                continue
        if mn in('shl','sar','shr') :
            d,sv=ops; regs[d]=f'({regs.get(d,d)} {"<<" if mn=="shl" else ">>"} {sv})'; continue
        if mn=='neg': regs[ops[0]]=f'-{regs.get(ops[0],ops[0])}'; continue
        if mn=='cwd': regs['dx']='sx(ax)'; continue
        if mn in('idiv','div'):
            dv=val(ops[0],regs); n=regs.get('ax','ax')
            regs['dx']=f'({n} % {dv})'; regs['ax']=f'({n} / {dv})'; continue
        if mn=='xchg' and set(ops)=={'ax','dx'}:
            regs['ax'],regs['dx']=regs.get('dx','dx'),regs.get('ax','ax'); continue
        if mn=='cmp':
            cmp=(val(ops[0],regs),val(ops[1],regs)); continue
        if mn in CC:
            emit(f'if ({cmp[0]} {CC[mn]} {cmp[1]}) goto L{int(ops[0],16):04X};'); continue
        if mn=='jmp':
            emit(f'goto L{int(ops[0],16):04X};'); continue
        if mn=='ret': emit(f'return {regs.get("ax","ax")};'); continue
        if mn=='mov' and ops==['sp','bp'] or (mn=='pop' and ops==['bp']): continue
        raise Exception
    except Exception as e:
        emit(f'asm("{t}");'+c)
DS=open(sys.argv[3],'rb').read() if len(sys.argv)>3 else None
def sat(a):
    b=b''
    while a<len(DS) and DS[a]!=0 and len(b)<70: b+=bytes([DS[a]]); a+=1
    return b.decode('latin1').replace('\x08','').replace('\n','\\n').replace('\r','\\r')
res=[]
for o in out:
    if isinstance(o,tuple) or o.startswith('    puts('):
        x=('    '+o[1]+';') if isinstance(o,tuple) else o
        m=re.match(r'\s*(puts)\((&g([0-9A-F]{4})|0x([0-9a-f]+)|(\d+))\)',(o[1] if isinstance(o,tuple) else o))
        if m and DS:
            a=int(m.group(3),16) if m.group(3) else int(m.group(4),16) if m.group(4) else int(m.group(5))
            x+='   // "'+sat(a)+'"'
        res.append(x)
    else: res.append(o)
# remove trivial 'if (x) goto A; goto B; A:' noise not attempted
print('\n'.join(res))
