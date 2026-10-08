import re, collections, sys
rep=open(sys.argv[1] if len(sys.argv)>1 else "drc-report.txt",encoding="utf-8",errors="replace").read()
cur=None; items=collections.defaultdict(set); cnt=collections.Counter()
for line in rep.splitlines():
    m=re.match(r'\[([a-z_]+)\]:', line)
    if m: cur=m.group(1); continue
    m=re.match(r'\s*@\(([-\d.]+) mm, ([-\d.]+) mm\): (.*)', line)
    if m and cur:
        cnt[cur]+=1
        items[cur].add(re.sub(r'\([^)]*\)','',m.group(3)).strip())
for k,v in cnt.most_common(): print("%-24s %d"%(k,v))
for k in sys.argv[2:] or ["courtyards_overlap","pth_inside_courtyard","npth_inside_courtyard","shorting_items","items_not_allowed","solder_mask_bridge","hole_to_hole","copper_edge_clearance","silk_edge_clearance","clearance"]:
    print("\n== "+k)
    for d in sorted(items[k]): print("   ", d)
