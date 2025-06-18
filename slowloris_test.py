#!/usr/bin/env python3

import socket
import time

HOST = "127.0.0.2"   # Change to your server's IP
PORT = 8004          # Your server's port
PATH = "/"           # Requested path

# Example HTTP GET request
request = (
    f"GET {PATH} HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

def slow_send(sock, data, delay=5):
    for byte in data.encode():
        sock.send(bytes([byte]))
        time.sleep(delay)  # Send one byte per `delay` seconds

def run():
    with socket.create_connection((HOST, PORT)) as sock:
        print(f"🚀 Starting slow send: {len(request)} bytes, 1 byte/sec")
        slow_send(sock, request, delay=5)

        print("📥 Waiting for response...")
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk

        print("\n✅ Response:")
        print(response.decode(errors="replace"))

if __name__ == "__main__":
    run()
