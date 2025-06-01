import socket
import time
def send_chunked_request():
    # Connect to the server
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect(('127.0.0.1', 8004))  # Replace with your server's address and port
    
    # Send the HTTP request with chunked transfer encoding
    headers = "POST /directory/youpi.bla HTTP/1.1\r\n"
    headers += "Host: localhost\r\n"  # Update with your server's host if needed
    headers += "Transfer-Encoding: chunked\r\n"
    headers += "Content-Type: text/plain\r\n"  # Specify the content type here
    headers += "\r\n"  # End of headers
    
    client_socket.send(headers.encode())
    
    # Send chunk 1 (size 5 bytes)
    client_socket.send(b"7\r\n, World\r\n")
    client_socket.send(b"5\r\nHello\r\n")
    time.sleep(1.5)
    # Send chunk 2 (size 7 bytes)
    client_socket.send(b"7\r\n, World\r\n")
    time.sleep(1.5)
    client_socket.send(b"7\r\n, World\r\n")
    time.sleep(1.5)
    # Send chunk 3 (size 0, indicating the end of the chunks)
    client_socket.send(b"0\r\n\r\n")
    time.sleep(1.5)
    # Receive the server's response
    response = client_socket.recv(1024).decode()
    print("Received response from server:")
    print(response)

    client_socket.close()

if __name__ == "__main__":
    send_chunked_request()
