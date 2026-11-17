#!/usr/bin/env python3
"""Serve one explicit media asset and record actual Safari media observations.

Use a LAN address, an existing non-DRM MP4 and a fresh private output directory.
The page reports real media events, play() rejections, currentTime and video
frame callbacks; none of these observations is automatically labelled PASS.
Only the tokenized page, media and event endpoint are served. No file browsing.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import ipaddress
import json
from pathlib import Path
import re
import secrets
import time


PAGE = r'''<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Safari video witness</title><style>body{font:20px sans-serif;background:#eee;color:#111}
video{width:90vw;max-height:60vh;background:#000}button{font-size:24px;margin:10px}pre{white-space:pre-wrap}</style>
<h1>Safari video witness</h1><p id="identity"></p>
<video id="v" controls playsinline muted loop preload="metadata" src="video.mp4"></video>
<p><button id="play">Play</button><button id="pause">Pause</button></p><pre id="status"></pre>
<script>
const v=document.getElementById('v');let callbacks=0,lastFrame=null,sequence=0;
document.getElementById('identity').textContent='RUN '+location.pathname.split('/')[1];
function report(kind,extra={}){
 const q=v.getVideoPlaybackQuality?v.getVideoPlaybackQuality():null;
 const row={sequence:++sequence,kind,wall:Date.now(),time:v.currentTime,duration:Number.isFinite(v.duration)?v.duration:null,
  paused:v.paused,ended:v.ended,ready:v.readyState,network:v.networkState,width:v.videoWidth,height:v.videoHeight,
  callbacks,lastFrame,quality:q?{total:q.totalVideoFrames,dropped:q.droppedVideoFrames}:null,
  error:v.error?{code:v.error.code,message:v.error.message}:null,...extra};
 document.getElementById('status').textContent=JSON.stringify(row,null,2);
 fetch('event',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(row),keepalive:true}).catch(()=>{});
}
function play(origin){const p=v.play();if(p&&p.then)p.then(()=>report('play-resolved',{origin})).catch(e=>report('play-rejected',{origin,name:e.name,message:e.message}));}
document.getElementById('play').onclick=()=>play('button');document.getElementById('pause').onclick=()=>{v.pause();report('pause-button');};
for(const name of ['loadstart','loadedmetadata','loadeddata','canplay','playing','pause','waiting','stalled','ended','error','seeking','seeked'])v.addEventListener(name,()=>report(name));
if(v.requestVideoFrameCallback){function frame(now,m){callbacks++;lastFrame={mediaTime:m.mediaTime,presentedFrames:m.presentedFrames};v.requestVideoFrameCallback(frame);}v.requestVideoFrameCallback(frame);}
report('page-loaded',{userAgent:navigator.userAgent});play('muted-autoplay');setInterval(()=>report('sample'),2000);
// Real pause and resume exercise the same HTMLMediaElement. Results, including
// policy rejection on resume, remain visible rather than forcing success.
setTimeout(()=>{v.pause();report('scheduled-pause');},20000);setTimeout(()=>play('scheduled-resume'),24000);
</script>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', required=True)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--asset', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=1800)
    args = parser.parse_args()
    address = ipaddress.ip_address(args.bind)
    if address.version != 4 or not address.is_private or address.is_unspecified or args.out.exists():
        parser.error('Use an explicit private IPv4 address and a fresh output directory')
    if not 1 <= args.duration <= 3600:
        parser.error('Duration must be 1..3600 seconds')
    media = args.asset.read_bytes()
    if not 12 <= len(media) <= 64 * 1024 * 1024 or media[4:8] != b'ftyp':
        parser.error('Use a real MP4 asset of at most 64 MiB')
    token = secrets.token_hex(8)
    args.out.mkdir(parents=True)
    events = (args.out / 'events.jsonl').open('a', buffering=1)
    deadline = time.monotonic() + args.duration

    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(5)

        def log_message(self, *_):
            pass

        def record(self, value):
            value.update(received_at=time.time(), peer=self.client_address[0],
                         user_agent=self.headers.get('User-Agent', '')[:512])
            events.write(json.dumps(value, ensure_ascii=False) + '\n')

        def deliver(self, head=False):
            if self.path == '/' + token + '/':
                body, content_type = PAGE.encode(), 'text/html; charset=utf-8'
            elif self.path == '/' + token + '/video.mp4':
                body, content_type = media, 'video/mp4'
            else:
                self.send_error(404)
                return
            start, end, status = 0, len(body) - 1, 200
            requested = self.headers.get('Range')
            if requested:
                match = re.fullmatch(r'bytes=(\d*)-(\d*)', requested)
                if not match or not (match[1] or match[2]) or content_type != 'video/mp4':
                    self.send_error(416)
                    return
                if match[1]:
                    start = int(match[1])
                    end = min(int(match[2]) if match[2] else end, end)
                else:
                    count = int(match[2])
                    start = max(0, len(body) - count) if count else len(body)
                if start > end:
                    self.send_response(416)
                    self.send_header('Content-Range', 'bytes */' + str(len(body)))
                    self.end_headers()
                    return
                status = 206
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Accept-Ranges', 'bytes')
            if status == 206:
                self.send_header('Content-Range', f'bytes {start}-{end}/{len(body)}')
            self.end_headers()
            if not head:
                self.wfile.write(body[start:end + 1])
            self.record({'kind': 'http', 'path': self.path, 'status': status,
                         'range': requested, 'bytes': end - start + 1, 'method': self.command})

        def do_GET(self):
            self.deliver()

        def do_HEAD(self):
            self.deliver(True)

        def do_POST(self):
            if self.path != '/' + token + '/event':
                self.send_error(404)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 16384:
                    raise ValueError('event size')
                value = json.loads(self.rfile.read(size))
                if not isinstance(value, dict):
                    raise ValueError('event object')
            except (ValueError, json.JSONDecodeError):
                self.send_error(400)
                return
            self.record({'kind': 'browser-observation', 'observation': value})
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()

    with HTTPServer((args.bind, args.port), Handler) as server:
        server.timeout = 1
        manifest = {'url': f'http://{args.bind}:{args.port}/{token}/',
                    'asset_sha256': hashlib.sha256(media).hexdigest(), 'asset_bytes': len(media),
                    'started_at': time.time(), 'duration_seconds': args.duration,
                    'acceptance': 'PENDING actual Safari identity, frame/content/control review'}
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(manifest['url'], flush=True)
        try:
            while time.monotonic() < deadline:
                server.handle_request()
        finally:
            events.close()


if __name__ == '__main__':
    main()
