"""tests/httpd.py DIR PORTFILE: serve DIR over HTTP for run.sh (GET; PUT saves
the body as DIR/put.out; /moved redirects to /publisher/amix/catalog/catalog.attrs).
Writes the port it got to PORTFILE."""
import functools, http.server, os, sys

class H(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/moved":
            self.send_response(302)
            self.send_header("Location", "http://127.0.0.1:%d/publisher/amix/catalog/catalog.attrs"
                             % self.server.server_address[1])
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def do_PUT(self):
        n = int(self.headers.get("Content-Length", 0))
        with open(os.path.join(self.directory, "put.out"), "wb") as f:
            f.write(self.rfile.read(n))
        self.send_response(204)
        self.end_headers()

srv = http.server.HTTPServer(("127.0.0.1", 0), functools.partial(H, directory=sys.argv[1]))
with open(sys.argv[2] + ".part", "w") as f:
    f.write(str(srv.server_address[1]))
os.rename(sys.argv[2] + ".part", sys.argv[2])
srv.serve_forever()
