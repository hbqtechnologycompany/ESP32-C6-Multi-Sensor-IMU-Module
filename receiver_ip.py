import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("", 12345))
while True:
    data, addr = s.recvfrom(1024)
    print(f"From {addr}: {data.decode()}")
