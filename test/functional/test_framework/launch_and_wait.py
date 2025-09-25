#!/usr/bin/env python3
# Copyright (c) The Bitcoin Core developers
"""Implements a shim to launch debuggers in place of bitcoind.

The commands for launching debuggers in a separate window tend to exit quickly,
continuing debugging in another process. This maps poorly to how the test
framework expects the bitcoind process to behave. This shim exists to account
for that requirement. It launches the debugger and then waits for a HTTP GET
with an optional parameter for desired exit code, and then exits back to the
test framework (to complete the test successfully if the exit code matches).

The drawback of this approach that while env might be able to be passed on in
some cases, stderr/stdout cannot be passed through.
"""

import http.server
import socketserver
import subprocess
import sys
import time

PORT = 8421

# Avoid "OSError: [Errno 98] Address already in use" upon repeated launches
socketserver.TCPServer.allow_reuse_address = True

class OneShotHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        exit_code = 0
        exit_code_header = self.headers.get('ExitCode')
        if exit_code_header:
            exit_code = int(exit_code_header)
        self.send_response(200)
        self.end_headers()
        message = f"GET received - exiting with code {exit_code}"
        self.wfile.write(message.encode())
        print(message)
        # Signal main thread to terminate
        self.server.exit_code = exit_code

    def log_message(self, format, *args):
        return  # This line effectively suppresses the log output

with socketserver.TCPServer(("", PORT), OneShotHandler) as httpd:
    print(f"Expecting this to exit immediately: {sys.argv[1:]}")
    process = subprocess.Popen(sys.argv[1:])
    result = process.wait()
    assert result == 0, "Failed with non-zero exit code: {result}"
    print("Executed subprocess successfully.")

    print(f'Just hanging around on port {PORT}, letting you debug. Send HTTP GET request to abort, for example through:\n'
          f'curl http://127.0.0.1:{PORT} --header "ExitCode: 3"')

    httpd.exit_code = None
    while httpd.exit_code is None:
        httpd.handle_request()

    sys.exit(httpd.exit_code)
