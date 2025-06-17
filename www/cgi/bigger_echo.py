#!/usr/bin/env python3

import sys
import os
import time

# Log helper
def log(message):
    with open("cgi_debug.log", "a") as f:
        f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {message}\n")
        f.flush()

log("CGI script started")

# CGI output header
print("Content-Type: text/plain\r\n\r\n")
print("")  # End of headers

log("Printed headers")

# Read stdin (request body)
body = sys.stdin.read()
body = body * 1000
log(f"Read {len(body)} bytes from stdin")

# Respond with a large response (repeat the input)
#for i in range(100):
sys.stdout.write(body)
#sys.stdout.flush()
#log(f"Wrote chunk {i + 1} of size {len(body)}")

log("CGI script completed")