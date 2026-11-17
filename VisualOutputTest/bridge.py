"""Loopback-only camera viewer; uses pyserial, no browser USB chooser."""
import argparse
import json
from pathlib import Path
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import serial

VIEWER = Path(__file__).resolve().parent / 'viewer'

class Camera:
    def __init__(self, port):
        self.usb = serial.Serial(port, 115200, timeout=0.25, write_timeout=2)
        self.lock = threading.Lock()
        self.failed = None

    def capture(self):
        with self.lock:
            if self.failed:
                return {'kind': 'error', 'message': self.failed}
            try:
                self.usb.write(b'n')
                deadline = time.monotonic() + 18
                buffer = bytearray()
                while time.monotonic() < deadline:
                    buffer.extend(self.usb.read(min(self.usb.in_waiting or 1, 4096)))
                    if len(buffer) > 100000:
                        raise ValueError('USB response exceeds size limit; reset XIAO and restart this server.')
                    while b'\n' in buffer:
                        line, _, remainder = buffer.partition(b'\n')
                        buffer = bytearray(remainder)
                        try:
                            result = json.loads(line)
                        except (ValueError, UnicodeDecodeError):
                            continue  # Boot log lines are not camera messages.
                        if isinstance(result, dict) and result.get('kind') in ('frame', 'error'):
                            if result['kind'] == 'error':
                                self.failed = result.get('message', 'Camera error')
                            return result
                raise TimeoutError('No camera response. Reset XIAO and restart this server.')
            except (OSError, serial.SerialException, ValueError, TimeoutError) as error:
                self.failed = str(error)
                return {'kind': 'error', 'message': self.failed}

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(VIEWER), **kwargs)

    def allowed(self, require_origin=False):
        origins = {f'http://localhost:{self.server.server_port}', f'http://127.0.0.1:{self.server.server_port}'}
        hosts = {item.removeprefix('http://') for item in origins}
        return (self.headers.get('Host') in hosts and
                (not require_origin or self.headers.get('Origin') in origins))

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        super().end_headers()

    def do_GET(self):
        if not self.allowed():
            self.send_error(403)
            return
        if self.path in ('/', '/index.html'):
            html = (VIEWER / 'index.html').read_text()
            html = html.replace('<script src="protocol.js">', '<script src="bridge.js"></script><script src="protocol.js">')
            body = html.encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path in ('/viewer.js', '/protocol.js', '/bridge.js'):
            super().do_GET()
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != '/capture' or not self.allowed(require_origin=True):
            self.send_error(403)
            return
        if self.headers.get('Content-Type') != 'application/json' or self.headers.get('Content-Length') != '2':
            self.send_error(400)
            return
        if self.rfile.read(2) != b'{}':
            self.send_error(400)
            return
        body = json.dumps(self.server.camera.capture()).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, format, *args):
        pass  # Do not log camera payloads or repetitive frame requests.

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='XIAO serial port, e.g. COM5 or /dev/ttyACM0')
    parser.add_argument('--http-port', type=int, default=8001)
    args = parser.parse_args()
    camera = Camera(args.port)
    server = ThreadingHTTPServer(('127.0.0.1', args.http_port), Handler)
    server.camera = camera
    print(f'Camera viewer ready: http://localhost:{args.http_port} (Ctrl+C to stop)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        camera.usb.close()

if __name__ == '__main__':
    main()
