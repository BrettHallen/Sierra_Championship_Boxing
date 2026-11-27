# Print the lines of a listing between two hex addresses: python3 seg.py X.asm 16EE 1A60
import sys
a0,a1=int(sys.argv[2],16),int(sys.argv[3],16)
for l in open(sys.argv[1]):
    try: a=int(l[:4],16)
    except: continue
    if a0<=a<=a1: print(l.rstrip())
