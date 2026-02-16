import socket
import struct
import time
import sys

MASTER_IP = "0.0.0.0"
PORT = 5000

def get_successor(ip):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        sock.connect((ip, PORT))
        header = struct.pack('<B B I', 0xCC, 0x0A, 0)
        sock.sendall(header)

        resp_hdr = sock.recv(6)
        magic, msg_type, p_len = struct.unpack('<B B I', resp_hdr)

        if msg_type == 0x0B:
            payload = sock.recv(p_len)
            next_node_ip_raw = payload[21:25]
            return socket.inet_ntoa(next_node_ip_raw)
        sock.close()
    except:
        return None

def walk_ring(NUM_NODES):
    print(f"Starting ring check at {MASTER_IP}...")
    current_ip = MASTER_IP
    visited = []

    for i in range(NUM_NODES * 2):
        visited.append(current_ip)
        next_ip = get_successor(current_ip)

        if not next_ip:
            print(f"Cancel: Node {current_ip} is not responding.")
            return

        print(f"Move {i+1}: {current_ip} -> {next_ip}")

        if next_ip == MASTER_IP:
            print(f"Ring closed! Found {len(visited)} nodes.")
            return

        if next_ip in visited:
            print(f"Short circuit found! Sub-Ring at {next_ip}.")
            return

        current_ip = next_ip

    print(f"Ring not closed or too many moves needed (max {NUM_NODES}).")

if __name__ == "__main__":
    MASTER_IP = sys.argv[1]
    if(len(sys.argv) > 2):
        NUM_NODES = sys.argv[2]
    else:
        NUM_NODES = 10
    walk_ring(int(NUM_NODES))