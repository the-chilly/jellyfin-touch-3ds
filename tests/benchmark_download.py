#!/usr/bin/env python3
"""Compare the actual download path against fast and server-paced local HTTPS.

Host throughput does not predict 3DS TLS or SD performance. No Jellyfin login
or external network is used. Requires Clang, Python, curl headers, OpenSSL.
"""
from pathlib import Path
import http.server
import ssl
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='jfin-download-bench-') as folder:
    temp = Path(folder)
    fixture = (ROOT / 'tests/fixtures/movie.ts').read_bytes()
    fast = fixture * 8
    paced = fixture[:256 * 1024]
    (temp / 'fast.ts').write_bytes(fast)
    (temp / 'paced.ts').write_bytes(paced)
    (temp / 'cert.conf').write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\n')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(temp / 'key.pem'), '-out', str(temp / 'ca.pem'), '-days', '1', '-config', str(temp / 'cert.conf')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sources = ['tests/benchmark_download.c', 'src/util/downloads.c', 'src/util/cache.c', 'src/api/cJSON.c', 'src/util/net.c']
    subprocess.run(['clang', '-std=c11', '-O2', '-DHOST_REAL_CLOCK', '-I' + str(ROOT / 'tests/host'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'include/api'), '-DNET_CA_PATH="' + str(temp / 'ca.pem') + '"', *(str(ROOT / f) for f in sources), '-lcurl', '-lm', '-pthread', '-o', str(temp / 'bench')], check=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            body = fast if self.path == '/fast' else paced
            self.send_response(200)
            self.send_header('Content-Type', 'video/mp2t')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            if self.path == '/fast':
                self.wfile.write(body)
            else:
                start = time.monotonic()
                for offset in range(0, len(body), 4096):
                    end = min(offset + 4096, len(body))
                    wait = end / (50 * 1024) - (time.monotonic() - start)
                    if wait > 0:
                        time.sleep(wait)
                    self.wfile.write(body[offset:end])
                    self.wfile.flush()

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(temp / 'ca.pem', temp / 'key.pem')
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for mode in ['fast', 'paced']:
            print(f'Local HTTPS source: {mode}', flush=True)
            subprocess.run([str(temp / 'bench'), f'https://localhost:{server.server_port}/{mode}', str(temp / f'{mode}.ts')], cwd=temp, check=True)
    finally:
        server.shutdown()
        server.server_close()
