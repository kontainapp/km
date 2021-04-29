# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
"""

Trivial HTTP server in python3.

Usage::
    km ./python.km  ../scripts/micro_srv [<port>]

Send a GET request::
    curl -s http://localhost[:port]

Send a HEAD request::
    curl -s -I http://localhost[:port]

Send a POST request::
    curl -s -d "some_stuff=good&other_stuff=bad" http://localhost[:port]
"""

import os
import json
import cgi
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    """
    This is the actual code called when GET/POST arrives
    """

    def _set_headers(self):
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()

    def do_GET(self):
        self._set_headers()
        uname = os.uname()
        self.wfile.write(json.dumps({'sysname': uname.sysname, 'nodename': uname.nodename,
                                     'release': uname.release, 'version': uname.version, 'machine': uname.machine, 'received': 'ok'}).encode())

    def do_HEAD(self):
        self._set_headers()

    def do_POST(self):
        # just send something back.
        self._set_headers()
        form = cgi.FieldStorage(fp=self.rfile, headers=self.headers,
                                environ={'REQUEST_METHOD': 'POST', 'CONTENT_TYPE': self.headers['Content-Type'], })
        self.wfile.write(json.dumps(
            {'Type': 'POST', 'from': self.headers.get('User-Agent'),
             'form':  {key: form.getvalue(key) for key in form},  'received': 'ok'}).encode())


def run(server_class=HTTPServer, handler_class=Handler, port=8080):
    httpd = server_class(('', port), handler_class)
    print('Listening on http://localhost:{0}...'.format(port))
    httpd.serve_forever()


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        run(port=int(sys.argv[1]))
    else:
        run()
