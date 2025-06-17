import sys
import time

print("Content-Type: text/plain\r\n\r\n",  end="")

while True:
    with open("test123.txt", "a") as f:
        f.write("A")
    time.sleep(1)
