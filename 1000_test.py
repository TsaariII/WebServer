#!/usr/bin/env python3

import socket
import multiprocessing
import time

HOST = '127.0.0.2'
PORT = 8004
CGI_PATH = '/cgi/echo.py'  # Change to a valid CGI path on your server

# Create a large POST body (1MB)
BODY_SIZE = 100 * 1024  # 1 MB
body = "x" * BODY_SIZE

# Prepare raw HTTP request
request = (
    f"POST {CGI_PATH} HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"Content-Length: {len(body)}\r\n"
    f"Content-Type: text/plain\r\n"
    f"Connection: close\r\n"
    f"\r\n"
    f"{body}"
).encode()


def send_request(client_id):
    try:
        with socket.create_connection((HOST, PORT), timeout=100) as sock:
            sock.sendall(request)
            response = b''
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
        print(f"✅ Client {client_id} finished, received {len(response)} bytes.")
        # print("\n🔽 Response:")
        # print(response.decode(errors="replace"))
    except Exception as e:
        print(f"❌ Client {client_id} failed: {e}")


def main():
    client_count = 1000  # You can reduce this for testing
    start_time = time.time()

    with multiprocessing.Pool(processes=500) as pool:  # Run up to 100 clients in parallel
        pool.map(send_request, range(client_count))

    print(f"\n⏱️ All clients done in {time.time() - start_time:.2f} seconds")


if __name__ == "__main__":
    main()
