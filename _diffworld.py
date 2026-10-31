import os, re

LIST = r"F:\9ymsgo\9you\viewer\_list_out.txt"
UNP = r"F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\unpackdat\world"

# declared per world sub-archive: key = relative path under data\world
decl = {}
pat = re.compile(r'^(.*?\\data\\world\\(.*?))\s+(\d+)\s+files\s')
with open(LIST, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        m = pat.match(line.rstrip('\n'))
        if not m: continue
        sub = m.group(2).replace('\\','/')
        n = int(m.group(3))
        decl[sub] = decl.get(sub, 0) + n

# count unpackdat/world per subfolder
def count_dir(d):
    c = 0
    for _,_,fs in os.walk(d): c += len(fs)
    return c

unp = {}
for d in os.listdir(UNP):
    full = os.path.join(UNP, d)
    if os.path.isdir(full):
        unp[d.lower()] = count_dir(full)

# map declared sub-archive -> unpackdat folder
# world/field.hed        -> field (direct, but nested also go under field/<Dxx>)
# world/field/D00.hed    -> field/D00
# world/object.hed       -> object
# world/filter.hed       -> filter
# world/sky.hed          -> sky
print("=== declared world sub-archives ===")
for k in sorted(decl):
    print("  %-22s %d" % (k, decl[k]))

print("\n=== unpackdat/world folders ===")
for k in sorted(unp):
    print("  %-22s %d" % (k, unp[k]))

# For field: sum nested subdirs vs top field.hed
field_nested = sum(v for k,v in unp.items() if k.startswith('field/') or (k not in ('field',) and k.startswith('field')))
field_top = unp.get('field', 0)
print("\nunpackdat field (top, no subdir) =", field_top)
print("unpackdat field nested subdirs total =", sum(v for k,v in unp.items() if '/' in k))
print("declared field.hed =", decl.get('field.hed',0))
print("declared field nested (field/<X>.hed) =", sum(v for k,v in decl.items() if k.startswith('field/') and k!='field.hed'))
print("TOTAL declared world =", sum(decl.values()), " TOTAL unpackdat world =", sum(unp.values()))
