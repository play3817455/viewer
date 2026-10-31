import os
DATA = r"F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\data"

# field.hed entry DM_D00_grd01_AAA.dxg -> data/world/field/0000.dat @0 size 19854
# D00.hed  entry DM_D00_grd01_AAA.dxg -> data/world/field/D00/0000.dat @0 size 19854
def read(p, off, n):
    with open(p,'rb') as f:
        f.seek(off); return f.read(n)

a = read(os.path.join(DATA,'world/field/0000.dat'), 0, 19854)
b = read(os.path.join(DATA,'world/field/D00/0000.dat'), 0, 19854)
print("field.hed  slice len", len(a))
print("D00.hed   slice len", len(b))
print("IDENTICAL:", a == b)

# Also compare a F-series: F01.hed FM_asF01_grd01_A.dxg -> world/field/F01/0000.dat @0 size 363920
# and field.hed path .\F01\model\FM_asF01_grd01_A.dxg -> world/field/0000.dat @ ? need offset
# We'll just compare the F01 one from its own volume vs from master by matching basename offset.
# Simpler: master field.hed has F01 entry at some offset; we don't have it handy, so compare D00 only here.
# Compare two DIFFERENT files to ensure not everything identical:
c = read(os.path.join(DATA,'world/field/0000.dat'), 19854, 2966)  # D00_grd01_AAB
d = read(os.path.join(DATA,'world/field/D00/0000.dat'), 19854, 2966)
print("second pair IDENTICAL:", c == d)
