#!/usr/bin/env python3
"""Production C logic with mock hardware; real curl HTTPS against local fixtures."""
from pathlib import Path
import http.server
import json
import ssl
import subprocess
import tempfile
import threading
import hashlib

ROOT = Path(__file__).resolve().parents[1]

def run(*args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)

def compile_test(destination, sources, extra=()):
    run('clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-variable', '-Wno-deprecated-declarations',
        '-DJFIN_VERSION="touch-0.4.1"', '-I' + str(ROOT / 'tests/host'),
        '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'include/api'), *extra,
        *(str(ROOT / s) for s in sources), '-lcurl', '-lm', '-pthread', '-o', str(destination))

class Fixture(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_POST(self):
        assert self.path == '/jellyfin/Users/AuthenticateByName'
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert body == {'Username': 'user', 'Pw': 'password'}
        self.reply({'AccessToken': 'test-token', 'User': {'Id': 'user-id'}})
    def do_GET(self):
        if self.path in ('/update/release','/update/asset'):
            body=bytearray(1024);body[:8]=b'3DSX\x20\x00\x00\x00'
            if self.path.endswith('release'):
                self.reply({'tag_name':'v9.0.0','draft':False,'prerelease':False,'assets':[{'name':'jellyfin-3ds.3dsx','size':len(body),'digest':'sha256:'+hashlib.sha256(body).hexdigest(),'browser_download_url':'https://github.com/the-chilly/jellyfin-touch-3ds/releases/download/v9.0.0/jellyfin-3ds.3dsx'}]});return
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return
        if '/download-' in self.path:
            route=self.path.split('/download-',1)[1]
            if route in ('movie','song'):
                body=(ROOT/'tests/fixtures'/('movie.ts' if route=='movie' else 'song.mp3')).read_bytes()
                self.send_response(200);self.send_header('Content-Type','video/mp2t' if route=='movie' else 'audio/mpeg')
                self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return
            body=b'{}' if route=='json' else b'abc' if route=='short' else b''
            self.send_response(500 if route=='error' else 200)
            self.send_header('Content-Type','application/json' if route=='json' else 'video/mp2t')
            self.send_header('Content-Length','100' if route=='short' else str(len(body)))
            self.end_headers();self.wfile.write(body);self.close_connection=True;return
        if '/shutdown-stall' in self.path:
            import time
            if self.path.endswith('-body'):
                self.send_response(200);self.send_header('Content-Length','10');self.end_headers();self.wfile.flush()
            time.sleep(4)
            return
        if self.path.startswith('/jellyfin/Items/'):
            assert self.headers['X-Emby-Token']=='test-token'
            if '/slow-poster/' in self.path:
                import time; time.sleep(1)
            image=bytes.fromhex('89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de0000000c49444154789c63f8cfc000000301010018dd8db00000000049454e44ae426082')
            self.send_response(200);self.send_header('Content-Length',str(len(image)));self.end_headers();self.wfile.write(image);return
        if self.path.startswith('/jellyfin/Users/user-id/Items?') or '/Items/Resume?' in self.path:
            assert 'test-token' in self.headers['Authorization']
            self.reply({'Items':[{'Id':'movie','Name':'Movie','Type':'Movie','ImageTags':{'Primary':'tag'}}], 'TotalRecordCount':1});return
        assert self.path.startswith('/jellyfin/Users/user-id/Items/')
        assert 'test-token' in self.headers['Authorization']
        request_path=self.path.split('?',1)[0]
        if request_path.endswith('/slow'):
            import time; time.sleep(.2)
        if request_path.endswith('/movie'):
            self.reply({'Id': 'movie', 'Name': 'Movie', 'Type': 'Movie',
                'ProductionYear': 2024, 'RunTimeTicks': 6000000000, 'Overview': 'M' * 5000})
            return
        if request_path.endswith('/track'):
            self.reply({'Id': 'track', 'Name': 'Song', 'Type': 'Audio', 'Album': 'Album',
                'Artists': ['Artist'], 'RunTimeTicks': 1800000000, 'Genres': ['Rock']})
            return
        self.reply({'Id': 'episode', 'Name': 'Episode three', 'Type': 'Episode',
            'SeriesName': 'Test series', 'SeriesId':'series-id', 'SeriesPrimaryImageTag':'tag', 'ParentIndexNumber': 2, 'IndexNumber': 3,
            'RunTimeTicks': 36000000000, 'Overview': 'An episode description.',
            'Genres': ['Drama', 'Sci-Fi'], 'OfficialRating': 'TV-PG',
            'CommunityRating': 8.5, 'UserData': {'PlaybackPositionTicks': 500000000}})
    def reply(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

class Redirect(Fixture):
    def do_POST(self):
        self.send_response(302)
        self.send_header('Location', 'http://127.0.0.1:1/leak')
        self.send_header('Content-Length', '0')
        self.end_headers()

def serve(cert, key, handler):
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server

with tempfile.TemporaryDirectory(prefix='jellyfin-touch-test-') as folder:
    temp = Path(folder)
    compile_test(temp / 'core', ['tests/test_core.c', 'src/ui/timeline.c', 'src/util/config.c'])
    run(str(temp / 'core'), cwd=temp)
    compile_test(temp / 'ui', ['tests/test_ui.c', 'src/ui/ui.c', 'src/ui/timeline.c'], ['-DHOST_APT_CLOSE'])
    run(str(temp / 'ui'), cwd=temp)
    (temp / 'cert.conf').write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n'
        '[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\n')
    for name in ('trusted', 'untrusted'):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
            '-keyout', str(temp / (name + '.key')), '-out', str(temp / (name + '.pem')),
            '-days', '1', '-config', str(temp / 'cert.conf')], check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    compile_test(temp / 'api', ['tests/test_api.c', 'src/api/jellyfin.c',
        'src/api/cJSON.c', 'src/util/net.c'], ['-DNET_CA_PATH="' + str(temp / 'trusted.pem') + '"'])
    compile_test(temp / 'art', ['tests/test_art.c', 'src/ui/album_art.c', 'src/util/stb_image_impl.c', 'src/util/cache.c', 'src/api/jellyfin.c', 'src/api/cJSON.c', 'src/util/net.c'], ['-DHOST_GPU_CHECK', '-DNET_CA_PATH="' + str(temp / 'trusted.pem') + '"'])
    compile_test(temp/'shutdown',['tests/test_shutdown.c','src/util/net.c'],['-DNET_CA_PATH="'+str(temp/'trusted.pem')+'"'])
    compile_test(temp/'downloads',['tests/test_downloads.c','src/util/downloads.c','src/util/cache.c','src/api/cJSON.c','src/util/net.c'],['-DNET_CA_PATH="'+str(temp/'trusted.pem')+'"'])
    server=serve(str(temp/'trusted.pem'),str(temp/'trusted.key'),Fixture)
    update_base=f'https://localhost:{server.server_port}/update'
    compile_test(temp/'update',['tests/test_update.c','src/util/update.c','src/util/sha256.c','src/util/net.c','src/api/cJSON.c'],['-DNET_CA_PATH="'+str(temp/'trusted.pem')+'"','-DUPDATE_API_ENDPOINT="'+update_base+'/release"','-DUPDATE_TEST_ASSET_URL="'+update_base+'/asset"','-DUPDATE_TARGET_PATH="app.3dsx"'])
    try:
        update_asset=bytearray(1024);update_asset[:8]=b'3DSX\x20\x00\x00\x00'
        run(str(temp/'update'),update_base,hashlib.sha256(update_asset).hexdigest(),cwd=temp)
        run(str(temp/'downloads'),f'https://localhost:{server.server_port}/jellyfin',str(ROOT/'tests/fixtures/movie.ts'),str(ROOT/'tests/fixtures/song.mp3'),cwd=temp)
        import shutil
        ffmpeg=shutil.which('ffmpeg')
        if not ffmpeg: raise RuntimeError('ffmpeg is required to validate the downloaded movie')
        run(ffmpeg,'-hide_banner','-loglevel','error','-i',str(temp/'downloaded-movie.ts'),'-f','null','-')
        run(ffmpeg,'-hide_banner','-loglevel','error','-ss','10','-i',str(temp/'downloaded-movie.ts'),'-t','1','-f','null','-')
        print('PASS: downloaded movie decodes with audio and seeks at 10 seconds on the host')
        run(str(temp/'art'),f'https://localhost:{server.server_port}/jellyfin',cwd=temp)
        run(str(temp/'shutdown'),f'https://localhost:{server.server_port}/jellyfin',cwd=temp)
    finally: server.shutdown();server.server_close()
    for name, host, mode, handler in (
        ('trusted', 'localhost', 'accept', Fixture),
        ('untrusted', 'localhost', 'reject', Fixture),
        ('trusted', '127.0.0.1', 'reject', Fixture),
        ('trusted', 'localhost', 'reject', Redirect),
    ):
        server = serve(str(temp / (name + '.pem')), str(temp / (name + '.key')), handler)
        try:
            run(str(temp / 'api'), f'https://{host}:{server.server_port}/jellyfin', mode, cwd=temp)
        finally:
            server.shutdown()
            server.server_close()
print('All host tests passed. Host tests do not verify 3DS linking, rendering or playback.')
