#!/usr/bin/env python3
"""Compare two folders of frame PNGs (same names).  Prints '0 different' or the list.
   python3 compare_frames.py REF_DIR NEW_DIR"""
import os, sys
from PIL import Image, ImageChops
ref, new = sys.argv[1], sys.argv[2]
names = sorted(f for f in os.listdir(ref) if f.endswith(".png"))
missing = [f for f in names if not os.path.exists(os.path.join(new, f))]
extra = sorted(f for f in os.listdir(new) if f.endswith(".png") and f not in names)
diff = []
for f in names:
    if f in missing: continue
    a = Image.open(os.path.join(ref, f)).convert("RGB")
    b = Image.open(os.path.join(new, f)).convert("RGB")
    if a.size != b.size or ImageChops.difference(a, b).getbbox() is not None:
        diff.append(f)
print("%d frames compared: %d different, %d missing, %d extra" % (len(names), len(diff), len(missing), len(extra)))
for f in diff[:20]: print("  different:", f)
for f in missing[:20]: print("  missing:", f)
sys.exit(1 if (diff or missing or extra) else 0)
