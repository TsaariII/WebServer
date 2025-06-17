#!/usr/bin/env python3
import socket
import time
def send_chunked_request():
    # Connect to the server
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect(('127.0.0.2', 8004))  # Replace with your server's address and port
    
    # Send the HTTP request with chunked transfer encoding
    headers = "POST /directory/youpi.bla HTTP/1.1\r\n "
    headers += "Host: localhost\r\n"  # Update with your server's host if needed
    headers += "Content-Length: 5\r\n"
    headers += "Content-Type: text/plain\r\n"  # Specify the content type here
    headers += "\r\n"  # End of headers
    headers += "abcde"
    
    client_socket.send(headers.encode())
    client_socket.send(headers.encode())

    response = client_socket.recv(1024).decode()
    print("Received response from server:")
    print(response)

    client_socket.close()

if __name__ == "__main__":
    send_chunked_request()
