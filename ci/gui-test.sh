#!/usr/bin/env bash
# Boot the live image with hs.gui=1 under UEFI (OVMF gives a GOP framebuffer), wait for HS-GUI-OK, take a screenshot via the QEMU monitor.
set -u
sudo apt-get install -y qemu-system-x86 >/dev/null
OVMF=$PWD/OVMF.fd; K=$PWD/build/out/bzImage.live; rm -f serial.log gui.ppm /tmp/mon
timeout 200 qemu-system-x86_64 -m 512 -machine q35 -bios "$OVMF" -kernel "$K" -append "hs.gui=1" \
  -drive file=out/homeserver-v0.4a-lite.img,format=raw,if=none,id=d,snapshot=on -device ahci,id=ahci -device ide-hd,drive=d,bus=ahci.0 \
  -vga std -display none -serial file:serial.log -monitor unix:/tmp/mon,server,nowait -no-reboot &
q=$!; ok=0
for i in $(seq 1 80); do grep -q HS-GUI-OK serial.log 2>/dev/null && { ok=1; break; }; sleep 2; done
if [ $ok = 1 ]; then
  sleep 3; python3 - <<'PY'
import socket,time
s=socket.socket(socket.AF_UNIX); s.connect('/tmp/mon'); time.sleep(.5); s.send(b'screendump gui.ppm\n'); time.sleep(2); s.send(b'quit\n')
PY
fi
kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
echo "=== serial log (GUI)"; tail -n 30 serial.log 2>/dev/null || echo "(no serial output)"
python3 - <<'PY'
import zlib,struct,os
if os.path.exists('gui.ppm'):
    d=open('gui.ppm','rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); px=parts[3]
    raw=b''.join(b'\0'+px[y*w*3:(y+1)*w*3] for y in range(h))
    def ch(t,b): return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
    open('out/gui-screenshot.png','wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
    print('screenshot saved',w,h)
PY
if [ -f out/gui-screenshot.png ]; then
  python3 - <<'PY'
import zlib,struct,base64
# half-size copy embedded as a log annotation (so the picture can be read without downloading artifacts)
d=open('gui.ppm','rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); px=parts[3]
w2,h2=w//2,h//2; rows=[]
for y in range(h2):
    r=bytearray()
    for x in range(w2): o=((y*2)*w+x*2)*3; r+=px[o:o+3]
    rows.append(b'\0'+bytes(r))
def ch(t,b): return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w2,h2,8,2,0,0,0))+ch(b'IDAT',zlib.compress(b''.join(rows),9))+ch(b'IEND',b'')
print('::notice title=gui-png::'+base64.b64encode(png).decode())
PY
fi
[ $ok = 1 ] || { echo "the graphical desktop did not start"; exit 1; }
