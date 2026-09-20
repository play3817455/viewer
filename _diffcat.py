import os, re

LIST = r"F:\9ymsgo\9you\viewer\_list_out.txt"
DATA = r"F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\data"
UNP = r"F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\unpackdat"

# parse list output: "<abspath>  N files  fmt=..."
decl = {}
pat = re.compile(r'^(.*?)\s+(\d+)\s+files\s+(.*)$')
with open(LIST, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        m = pat.match(line.rstrip('\n'))
        if not m: continue
        path = m.group(1).strip()
        n = int(m.group(2))
        # category = first dir component after "\data\"
        idx = path.lower().find('\\data\\')
        if idx < 0:
            idx = path.lower().find('/data/')
        rel = path[idx+6:] if idx >= 0 else path
        rel = rel.replace('\\','/')
        top = rel.split('/')[0].lower() if rel else '(root)'
        decl[top] = decl.get(top, 0) + n

# count unpackdat per top category
unp = {}
for d in os.listdir(UNP):
    full = os.path.join(UNP, d)
    if not os.path.isdir(full): continue
    c = 0
    for _,_,files in os.walk(full):
        c += len(files)
    unp[d.lower()] = c

cats = sorted(set(decl) | set(unp))
print("%-12s %10s %10s %10s" % ("category","declared","extracted","diff"))
tot_d = tot_u = 0
for c in cats:
    dd = decl.get(c,0); uu = unp.get(c,0)
    tot_d += dd; tot_u += uu
    flag = "  <-- SHORT" if uu < dd else ""
    print("%-12s %10d %10d %10d%s" % (c, dd, uu, dd-uu, flag))
print("%-12s %10d %10d %10d" % ("TOTAL", tot_d, tot_u, tot_d-tot_u))
