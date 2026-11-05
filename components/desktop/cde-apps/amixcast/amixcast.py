#!/usr/bin/env python3
"""amixcast.py -- the PC's half of streaming to the A3000.

Lists a folder of this PC, ~/Videos unless told otherwise, or a DLNA server,
and has VLC transcode what the Amiga picks, at the bitrate and from the time
it asks for.  The Amiga only speaks plain HTTP
to this program:

  GET /list?id=0                 a folder, one line each, tab separated:
                                   d <id> <title>
                                   f <id> <seconds, 0: unknown> <v|a|i> <title>
  GET /stream?id=..&t=..&kbit=..&w=.. MPEG-TS, H.264 + AAC, from t seconds on, w wide
  GET /info?id=..                the file's length in seconds (0: unknown)
  GET /probe                     1 MB, to measure the line

  amixcast.py [--dir ~/Videos | --server http://192.168.0.50:45227 | --dlna [NAME]]
              [--port 8092] [--width 640]

--dlna finds the DLNA servers on the network (SSDP): the only one, or the one
whose name has NAME in it; with more than one and no NAME, it lists them.
"""
import argparse, html, http.server, os, pathlib, re, shutil, socket, socketserver, subprocess, sys, time, \
    urllib.parse, urllib.request

BROWSE = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
          's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
          '<u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><ObjectID>%s</ObjectID>'
          '<BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter><StartingIndex>%d</StartingIndex>'
          '<RequestedCount>200</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>')


SEARCH = ('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 2\r\n'
          'ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n')


def discover(wait=3.0):
    """The media servers that answer an SSDP search: [(name, description URL)]."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    found, end = [], time.time() + wait
    for _ in range(2):              # UDP: ask twice
        s.sendto(SEARCH.encode(), ('239.255.255.250', 1900))
    while time.time() < end:
        s.settimeout(max(end - time.time(), 0.01))
        try:
            data = s.recv(4096).decode(errors='replace')
        except socket.timeout:
            break
        m = re.search(r'^location:\s*(\S+)', data, re.I | re.M)
        if m and m.group(1) not in [u for _, u in found]:
            try:
                d = urllib.request.urlopen(m.group(1), timeout=5).read().decode(errors='replace')
            except OSError:
                continue
            if 'ContentDirectory' in d:
                n = re.search(r'<friendlyName>([^<]*)', d)
                found.append((html.unescape(n.group(1)) if n else m.group(1), m.group(1)))
    s.close()
    return found


class Dlna:
    """A DLNA server's folders; remembers each file's URL by its id."""

    def __init__(self, location):
        """location: the server's description URL, or its base URL"""
        if urllib.parse.urlsplit(location).path in ('', '/'):
            location = location.rstrip('/') + '/description.xml'
        self.urls = {}
        self.lengths = {}
        d = urllib.request.urlopen(location, timeout=8).read().decode(errors='replace')
        m = re.search(r'ContentDirectory.*?<controlURL>([^<]+)', d, re.S)
        b = re.search(r'<URLBase>([^<]+)', d)
        self.control = urllib.parse.urljoin(b.group(1) if b else location, m.group(1).strip())

    def _page(self, oid, start):
        body = BROWSE % (html.escape(oid), start)
        req = urllib.request.Request(self.control, body.encode(), {
            'Content-Type': 'text/xml; charset="utf-8"',
            'SOAPAction': '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"'})
        return html.unescape(urllib.request.urlopen(req, timeout=15).read().decode(errors='replace'))

    def length(self, oid):
        """The server gives no durations: GStreamer looks at the file, once."""
        if oid not in self.lengths and oid in self.urls:
            try:
                o = subprocess.run(['gst-discoverer-1.0', '-t', '20', self.urls[oid]], capture_output=True,
                                   text=True, timeout=30).stdout
                m = re.search(r'Duration:\s*(\d+):(\d+):(\d+)', o)
                self.lengths[oid] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) if m else 0
            except subprocess.TimeoutExpired:
                return 0
        return self.lengths.get(oid, 0)

    def browse(self, oid):
        out, start = [], 0
        while True:
            x = self._page(oid, start)
            for m in re.finditer(r'<container\b([^>]*)>(.*?)</container>', x, re.S):
                out.append(('d', _attr(m.group(1), 'id'), _title(m.group(2)), 0, ''))
            for m in re.finditer(r'<item\b([^>]*)>(.*?)</item>', x, re.S):
                res = re.search(r'<res\b([^>]*)>([^<]+)', m.group(2))
                if not res:
                    continue
                self.urls[_attr(m.group(1), 'id')] = res.group(2).strip()
                out.append(('f', _attr(m.group(1), 'id'), _title(m.group(2)), _secs(_attr(res.group(1), 'duration')),
                            _kind(_attr(res.group(1), 'protocolInfo'))))
            n = re.search(r'<NumberReturned>(\d+)', x)
            total = re.search(r'<TotalMatches>(\d+)', x)
            start += int(n.group(1)) if n else 0
            if not n or not total or not int(n.group(1)) or start >= int(total.group(1)):
                return out


class Folder(Dlna):
    """A folder of this PC, listed like a DLNA server; ids are handed out as
    folders are listed, so nothing outside it can be asked for."""

    KINDS = {'v': 'mp4 mkv avi mov webm mpg mpeg m4v ts wmv flv ogv', 'a': 'mp3 flac ogg m4a wav opus aac',
             'i': 'jpg jpeg png gif'}

    def __init__(self, root):
        self.urls = {}
        self.lengths = {}
        self.dirs = {'0': pathlib.Path(root).expanduser().resolve()}
        self.next_id = 1

    def _id(self):
        self.next_id += 1
        return str(self.next_id - 1)

    def browse(self, oid):
        out = []
        for e in sorted(os.scandir(self.dirs[oid]), key=lambda e: (not e.is_dir(), e.name.lower())):
            if e.name.startswith('.'):
                continue
            title = e.name.replace('\t', ' ').replace('\n', ' ')
            if e.is_dir():
                i = self._id()
                self.dirs[i] = pathlib.Path(e.path)
                out.append(('d', i, title, 0, ''))
                continue
            ext = e.name.rsplit('.', 1)[-1].lower()
            kind = next((k for k, v in self.KINDS.items() if ext in v.split()), None)
            if kind:
                i = self._id()
                self.urls[i] = pathlib.Path(e.path).resolve().as_uri()
                out.append(('f', i, title, 0, kind))
        return out


def _attr(s, name):
    m = re.search(r'\b%s="([^"]*)"' % name, s)
    return m.group(1) if m else ''


def _title(s):
    m = re.search(r'<dc:title>([^<]*)', s)
    return m.group(1).replace('\t', ' ').replace('\n', ' ') if m else '?'


def _kind(info):
    m = re.search(r':(video|audio|image)/', info)
    return m.group(1)[0] if m else '?'


def _secs(d):
    m = re.match(r'(\d+):(\d+):(\d+)', d)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) if m else 0


def vlc_args(url, start, kbit, width):
    audio = 64 if kbit < 500 else 96
    video = max(150, kbit - audio - 30)         # the rest is MPEG-TS
    sout = ('#transcode{vcodec=h264,venc=x264{profile=high,preset=medium,bframes=3,keyint=75,'
            'vbv-maxrate=%d,vbv-bufsize=%d},vb=%d,fps=25,width=%d,acodec=mp4a,ab=%d,channels=2,'
            'samplerate=44100}:std{access=file,mux=ts,dst=-}' % (video * 11 // 10, video * 2, video, width, audio))
    return ['cvlc', '-q', '--no-video-title-show', url, '--start-time=%d' % start, '--sout', sout, 'vlc://quit']


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'

    def log_message(self, fmt, *a):
        sys.stderr.write('amixcast: %s %s\n' % (self.client_address[0], fmt % a))

    def text(self, s):
        b = s.encode('latin-1', 'replace')      # what Motif on the Amiga shows
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
        try:
            if u.path == '/list':
                rows = self.server.dlna.browse(q.get('id', '0'))
                self.text(''.join('d\t%s\t%s\n' % (r[1], r[2]) if r[0] == 'd' else
                                  'f\t%s\t%d\t%s\t%s\n' % (r[1], r[3], r[4], r[2]) for r in rows))
            elif u.path == '/info':
                self.text('%d\n' % self.server.dlna.length(q.get('id', '')))
            elif u.path == '/probe':
                self.send_response(200)
                self.send_header('Content-Length', str(1 << 20))
                self.end_headers()
                self.wfile.write(b'\x55' * (1 << 20))
            elif u.path == '/stream':
                self.stream(q)
            else:
                self.send_error(404)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def stream(self, q):
        url = self.server.dlna.urls.get(q.get('id', ''))
        if not url:
            self.send_error(404, 'list the folder first')
            return
        old = self.server.active.pop(self.client_address[0], None)
        if old:                 # a seek: the last stream of this client is over
            old.kill()
        # the width the Amiga asks for (with its bitrate), else the server's
        width = max(320, min(1920, int(q.get('w', self.server.width)))) & ~15
        p = subprocess.Popen(vlc_args(url, int(float(q.get('t', 0))), int(q.get('kbit', 600)), width),
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.server.active[self.client_address[0]] = p
        self.send_response(200)
        self.send_header('Content-Type', 'video/mp2t')
        self.end_headers()
        try:
            shutil.copyfileobj(p.stdout, self.wfile, 16384)
        finally:
            p.kill()
            p.wait()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    a = argparse.ArgumentParser()
    a.add_argument('--dir', default='~/Videos')
    a.add_argument('--server')
    a.add_argument('--dlna', nargs='?', const='', metavar='NAME')
    a.add_argument('--port', type=int, default=8092)
    a.add_argument('--width', type=int, default=640)
    o = a.parse_args()
    if o.dlna is not None:
        found = discover()
        pick = [f for f in found if o.dlna.lower() in f[0].lower()]
        if len(pick) != 1:
            print('amixcast: %s; --dlna NAME picks one:' % ('no DLNA server found' if not found else
                  'no server named "%s"' % o.dlna if not pick else 'more than one DLNA server'), file=sys.stderr)
            for n, u in found:
                print('  %s  %s' % (n, u), file=sys.stderr)
            sys.exit(2)
        o.server = pick[0][1]
        print('amixcast: DLNA server %s' % pick[0][0], flush=True)
    s = Server(('', o.port), Handler)
    s.dlna = Dlna(o.server) if o.server else Folder(o.dir)
    s.width = o.width
    s.active = {}
    print('amixcast: %s on port %d' % (o.server or o.dir, o.port), flush=True)
    s.serve_forever()


if __name__ == '__main__':
    main()
