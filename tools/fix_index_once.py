#!/usr/bin/env python3
from pathlib import Path
p=Path('web/index.html')
s=p.read_text(encoding='utf-8')
tag='<script src="/trade-incidence.js"></script>\n'
count=s.count(tag)
if count < 1:
    raise SystemExit('trade-incidence script tag missing')
if count != 1:
    s=s.replace(tag,'')
    anchor='<script src="/app.js"></script>\n'
    if s.count(anchor)!=1:
        raise SystemExit('app.js anchor not unique')
    s=s.replace(anchor,anchor+tag)
    p.write_text(s,encoding='utf-8')
print(f'normalized trade-incidence script tags from {count} to 1')
