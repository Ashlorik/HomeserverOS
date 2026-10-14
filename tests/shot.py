#!/usr/bin/env python3
"""shot.py NAME... : render /tmp/shots/NAME.json (pyte buffer) to PNG with headless chromium"""
import json, sys, html, os
from playwright.sync_api import sync_playwright
PAL = {'black':'#1e1e2e','red':'#c4506b','green':'#4f9d69','brown':'#c59d4a','blue':'#3b5fa8','magenta':'#8e5fb0','cyan':'#3d9aa8','white':'#b4bce0',
       'brightblack':'#6c7086','brightred':'#f38ba8','brightgreen':'#a6e3a1','brightbrown':'#f9e2af','brightblue':'#89b4fa','brightmagenta':'#cba6f7','brightcyan':'#94e2d5','brightwhite':'#f5f7ff'}
def col(c, bold, bg=False):
    if c == 'default': return '#1e1e2e' if bg else '#cdd6f4'
    if c in PAL: return PAL['bright'+c] if (bold and not bg and c != 'black' and 'bright'+c in PAL) else PAL[c]
    return '#' + c if len(c) == 6 else '#cdd6f4'
src = os.environ.get('SHOTS', '/tmp/shots'); out = os.environ.get('SHOTOUT', src)
with sync_playwright() as p:
    b = p.chromium.launch(); pg = b.new_page(viewport={'width': 960, 'height': 620})
    for name in sys.argv[1:]:
        rows = json.load(open(f'{src}/{name}.json')); h = []
        for r in rows:
            for ch, fg, bg, bold, rev in r:
                f, g = col(fg, bold), col(bg, False, True)
                if rev: f, g = g, f
                h.append(f'<span style="color:{f};background:{g};{"font-weight:bold" if bold else ""}">{html.escape(ch)}</span>')
            h.append('\n')
        pg.set_content(f'<body style="margin:0;background:#1e1e2e"><pre style="margin:10px;font:15px/17px \'DejaVu Sans Mono\',monospace">{"".join(h)}</pre>')
        pg.screenshot(path=f'{out}/{name}.png', clip={'x': 0, 'y': 0, 'width': 960, 'height': 560})
    b.close()
