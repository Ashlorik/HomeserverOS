#!/usr/bin/env python3
"""Drive a text program in a chroot through a pty and render the screen with pyte.
   term.py ROOT 'steps.py' -- the steps file defines CMD (list) and STEPS = [(delay_s, keys_or_None, 'snapshot-name'|None), ...]"""
import os, pty, sys, time, select, struct, fcntl, termios, json, subprocess
sys.path[:0] = ['/home/claude/hs3/tools/pyte', '/home/claude/hs3/tools/wcwidth']
import pyte
root, stepfile = sys.argv[1], sys.argv[2]; ns = {}; exec(open(stepfile).read(), ns)
COLS, ROWS = ns.get('SIZE', (100, 30)); outdir = ns.get('OUT', '/tmp/shots'); os.makedirs(outdir, exist_ok=True)
screen = pyte.Screen(COLS, ROWS); stream = pyte.ByteStream(screen)
here = os.path.dirname(os.path.abspath(__file__))
argv = ['unshare', '-m', '-p', '-f'] + ([] if ns.get('NET') else ['-n']) + ['sh', here + '/enter.sh', root] + ns['CMD']
env = dict(os.environ, TERM='linux', **ns.get('ENV', {}))
pid, fd = pty.fork()
if pid == 0: os.execvpe(argv[0], argv, env)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLS, 0, 0))
def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: d = os.read(fd, 65536)
            except OSError: return False
            if not d: return False
            stream.feed(d)
    return True
def text(): return '\n'.join(l.rstrip() for l in screen.display).rstrip()
def dump(name):
    open(f'{outdir}/{name}.txt', 'w').write(text())
    rows = []
    for y in range(ROWS):
        line = screen.buffer[y]; rows.append([(line[x].data, line[x].fg, line[x].bg, line[x].bold, line[x].reverse) for x in range(COLS)])
    json.dump(rows, open(f'{outdir}/{name}.json', 'w'))
alive = pump(ns.get('BOOT', 2))
for delay, keys, name in ns['STEPS']:
    if keys: os.write(fd, keys.encode().decode('unicode_escape').encode('latin1'))
    alive = pump(delay)
    if name: dump(name); print(f'--- {name}'); print(text())
try: os.kill(pid, 9)
except OSError: pass
subprocess.run(['pkill', '-9', '-f', 'enter.sh ' + root], check=False)
