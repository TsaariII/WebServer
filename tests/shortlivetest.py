#!/usr/bin/env python3
import socket
import time

s = socket.socket()
s.connect(('127.0.0.1', 8004))

req = "GET / HTTP/1.1\r\n"

# Send one character at a time
for c in req:
	s.send(c.encode())
	time.sleep(0.3)  # 1.5 seconds between each byte (too slow)

# Finish headers
s.send(b"\r\n\r\n")