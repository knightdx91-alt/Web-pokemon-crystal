#!/usr/bin/env python3
"""Tiny dev server for web/ that gzip-compresses responses.

The recompiled .wasm is large (~31 MB) but highly repetitive generated code, so it
gzips to ~4 MB. Browsers decompress transparently via Content-Encoding: gzip, so the
download is small. Real hosts (GitHub Pages, nginx, ...) do this automatically; this
server just provides it for `make serve` locally.

    python3 build/serve.py [port]   (default 8000)  -> http://localhost:<port>
"""
import gzip, io, os, sys, http.server, socketserver

WEB = os.path.join(os.path.dirname(__file__), "..", "web")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
GZIP_EXT = (".wasm", ".js", ".html", ".css")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=os.path.abspath(WEB), **k)

    def end_headers(self):
        # let the wasm be cached aggressively; it changes only on rebuild
        if self.path.endswith(".wasm"):
            self.send_header("Cache-Control", "public, max-age=31536000")
        super().end_headers()

    def copyfile(self, source, outputfile):
        if "gzip" in self.headers.get("Accept-Encoding", "") and self.path.endswith(GZIP_EXT):
            data = source.read()
            buf = io.BytesIO(); gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=6).write(data)
            outputfile.write(buf.getvalue())
        else:
            super().copyfile(source, outputfile)

    def send_head(self):
        # Recompute Content-Length for the gzipped body and add the encoding header.
        path = self.translate_path(self.path)
        if (os.path.isfile(path) and self.path.endswith(GZIP_EXT)
                and "gzip" in self.headers.get("Accept-Encoding", "")):
            with open(path, "rb") as f:
                raw = f.read()
            buf = io.BytesIO(); gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=6).write(raw)
            body = buf.getvalue()
            ctype = self.guess_type(path)
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(body)))
            if self.path.endswith(".wasm"):
                self.send_header("Cache-Control", "public, max-age=31536000")
            self.end_headers()
            return io.BytesIO(body)
        return super().send_head()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


if __name__ == "__main__":
    print(f"serving web/ on http://localhost:{PORT}  (gzip on; ~31MB wasm -> ~4MB over the wire)")
    Server(("", PORT), Handler).serve_forever()
