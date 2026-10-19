#!/bin/sh
echo "== Address"; ip -4 -o addr show scope global | awk '{print $2": "$4}'
echo "== Gateway"; ip route | awk '/default/{print $3; exit}'
echo "== DNS / internet"; ping -c2 -W3 1.1.1.1 >/dev/null 2>&1 && echo "ping 1.1.1.1: ok" || echo "ping 1.1.1.1: FAILED"
ping -c2 -W3 example.com >/dev/null 2>&1 && echo "name lookup: ok" || echo "name lookup: FAILED"
echo "== Download speed"
curl -s -o /dev/null -m 20 -w 'speed: %{speed_download} bytes/s (%{size_download} bytes in %{time_total}s)\n' https://speed.cloudflare.com/__down?bytes=10000000 || echo "speed test failed"
echo; read -r _
