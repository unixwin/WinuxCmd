import subprocess, time, os, sys, hashlib
# 64 MiB sed benchmark (#1488 sed lane): rust2 release driver vs the C++
# dispatcher (argv[1], e.g. build-rel/usr/bin/winuxcmd.exe from
# scripts/build-with-vs.ps1 -Configuration Release) vs WSL GNU sed 4.9 on
# ext4. Best of 3 wall-clock runs, output to a file; the sha1 column
# checks every side produced GNU's bytes.
#   cargo build --release -p winuxcmd-driver
#   python gate/sed-bench.py [CPP_EXE]
import random
GATE=os.path.dirname(os.path.abspath(__file__))
RUST2=os.path.dirname(GATE)
HERE=os.path.join(RUST2,'target','bench')
os.makedirs(HERE,exist_ok=True)
IN=os.path.join(HERE,'in64.txt')
RUST=os.path.join(RUST2,'target','release','winuxcmd-driver.exe')
def mnt(p):
    p=os.path.abspath(p).replace('\\','/')
    return '/mnt/'+p[0].lower()+p[2:]
if not os.path.exists(IN):
    random.seed(1488)
    words=["the","quick","brown","fox","jumps","over","lazy","dog","foo","bar","abc","cab","ab","baz","lorem","ipsum","dolor","sit","amet","re","x1y2","42"]
    out=[]; size=0; target=64*1024*1024
    while size<target:
        line=" ".join(random.choice(words) for _ in range(random.randint(4,14)))+"\n"
        out.append(line); size+=len(line)
    data="".join(out)[:target]
    open(IN,'w',newline='\n').write(data[:data.rfind("\n")+1])
CPP=sys.argv[1] if len(sys.argv)>1 else None
cases=[("s/foo/bar/g",["s/foo/bar/g"]),("s/\\(a\\)\\(b\\)/\\2\\1/",["s/\\(a\\)\\(b\\)/\\2\\1/"]),
       ("-n /re/p",["-n","/lorem.*amet/p"]),("y/abc/xyz/",["y/abc/xyz/"]),("$!N",["$!N"])]
def run(cmd, out):
    best=None
    for _ in range(3):
        with open(out,'wb') as fo:
            t=time.perf_counter(); subprocess.run(cmd,stdout=fo,check=False); dt=time.perf_counter()-t
        best=dt if best is None else min(best,dt)
    return best, hashlib.sha1(open(out,'rb').read()).hexdigest()[:10]
# GNU in WSL on ext4
gsh=os.path.join(HERE,'gnu.sh')
with open(gsh,'w',newline='\n') as f:
    f.write(f'cp {mnt(IN)} /tmp/in64.txt\n')
    for idx,(name,args) in enumerate(cases):
        q=' '.join("'"+a+"'" for a in args)
        f.write(f'best=999; for i in 1 2 3; do s=$(date +%s.%N); sed {q} /tmp/in64.txt > /tmp/o.txt; e=$(date +%s.%N); best=$(echo "$e - $s" | bc -l | awk -v b=$best \'{{print ($1<b)?$1:b}}\'); done; echo "{idx}|$best|$(sha1sum /tmp/o.txt | cut -c1-10)"\n')
g=subprocess.run(['wsl','bash',mnt(gsh)],capture_output=True,text=True,env=dict(os.environ,MSYS_NO_PATHCONV='1')).stdout
gres={l.split('|')[0]:l.split('|')[1:] for l in g.strip().splitlines() if '|' in l}
print(f"{'case':28} {'rust2':>8} {'C++':>8} {'GNU':>8}  outputs-equal")
for idx,(name,args) in enumerate(cases):
    r,rh=run([RUST,'sed']+args+[IN],os.path.join(HERE,'o_rust.txt'))
    c,ch=(run([CPP,'sed']+args+[IN],os.path.join(HERE,'o_cpp.txt')) if CPP else (float('nan'),'-'))
    gt,gh=gres.get(str(idx),['nan','-'])
    print(f"{name:28} {r:8.2f} {c:8.2f} {float(gt):8.2f}  rust=gnu:{rh==gh} cpp=gnu:{ch==gh}")
