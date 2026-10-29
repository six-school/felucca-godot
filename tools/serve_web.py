#!/usr/bin/env python3
"""A local server for the web export: the cross-origin isolation headers a threaded Godot web
build needs (COOP and COEP: its threads share memory through SharedArrayBuffer), nothing cached. (The "Web tests"
export runs the test its page's ?scene= names: demo/tests/web_tests.gd.)

    tools/serve_web.py DIR [PORT]      (default 8060, on 127.0.0.1; prints the URL)
"""
import functools, http.server, sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "application/javascript", ".pck": "application/octet-stream"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *a):
        pass


def main(argv):
    if len(argv) not in (2, 3):
        sys.stderr.write(__doc__)
        return 2
    port = int(argv[2]) if len(argv) == 3 else 8060
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=argv[1]))
    print(f"serving {argv[1]} at http://127.0.0.1:{server.server_address[1]}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
