"""Start the existing USB camera viewer on localhost."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import webbrowser


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    viewer = Path(__file__).resolve().parent / 'viewer'
    handler = partial(SimpleHTTPRequestHandler, directory=str(viewer))
    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler)
    except OSError as error:
        parser.exit(1, f'Cannot start viewer: {error}\nStop the previous server or choose another --port.\n')
    url = f'http://localhost:{args.port}'
    print(f'Viewer ready: {url}\nOpen in Chrome or Edge and click Connect XIAO.\nKeep this terminal open. Ctrl+C stops the viewer.', flush=True)
    if not args.no_browser:
        try:
            webbrowser.open(url)
        except webbrowser.Error:
            print(f'Open {url} in Chrome or Edge.', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
