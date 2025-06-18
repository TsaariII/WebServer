#!/usr/bin/env python3

import socket
import time

HOST = "127.0.0.2"  # Your server IP
PORT = 8004         # Your server port
PATH = "/"          # Resource path

# Basic HTTP GET request
request = (
    f"GET {PATH} HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"Connection: close\r\n"
    f"\r\n"
)

def run():
    with socket.create_connection((HOST, PORT)) as sock:
        print(f"📤 Sending request ({len(request)} bytes)")
        sock.sendall(request.encode())

        print("🐌 Reading response slowly (1 byte/sec)...")
        response = b""
        while True:
            try:
                byte = sock.recv(1)  # Read one byte at a time
                if not byte:
                    break
                response += byte
                print(byte.decode(errors="replace"), end="", flush=True)
                time.sleep(1)  # Delay between each byte read
            except socket.timeout:
                print("\n⏱️ Timed out while waiting for data")
                break

        print("\n\n✅ Finished receiving")

if __name__ == "__main__":
    run()
