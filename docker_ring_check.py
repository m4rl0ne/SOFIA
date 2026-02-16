import socket
import struct
import time
import sys

MASTER_IP = "0.0.0.0"
PORT = 5000

MSG_GET_SUCLIST = 0x0A
MSG_SUCLIST_RESP = 0x0B
MSG_GET_CERT = 0x0C
MSG_CERT_RESP = 0x0D

def get_certificate(ip):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        sock.connect((ip, PORT))

        header = struct.pack('<B B I', 0xCC, MSG_GET_CERT, 0)
        sock.sendall(header)

        resp_hdr = sock.recv(6)
        if len(resp_hdr) < 6: return "ERR_HEADER"

        magic, msg_type, p_len = struct.unpack('<B B I', resp_hdr)

        if msg_type == MSG_CERT_RESP:
            payload = sock.recv(p_len)

            if len(payload) >= 4:
                cert_len = struct.unpack('<I', payload[:4])[0]
                cert_data = payload[4:4+cert_len].decode('utf-8', errors='ignore')

                if cert_len == 0:
                    return "[EMPTY]"
                return cert_data

        return "WRONG_MSG_TYPE"
    except Exception as e:
        return f"TIMEOUT/ERR"
    finally:
        sock.close()

def get_successor(ip):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        sock.connect((ip, PORT))

        header = struct.pack('<B B I', 0xCC, MSG_GET_SUCLIST, 0)
        sock.sendall(header)

        resp_hdr = sock.recv(6)
        magic, msg_type, p_len = struct.unpack('<B B I', resp_hdr)

        if msg_type == MSG_SUCLIST_RESP:
            payload = sock.recv(p_len)
            next_node_ip_raw = payload[21:25]
            return socket.inet_ntoa(next_node_ip_raw)
        sock.close()
    except:
        return None

def walk_ring(start_ip, num_nodes):
    print(f"\n--- Starting Ring-Check & Cert-Audit at {start_ip} ---\n")
    print(f"{'STEP':<5} | {'CURRENT IP':<15} | {'NEXT IP':<15}  | {'CERTIFICATE STATUS'}")
    print("-" * 75)

    current_ip = start_ip
    visited = []

    for i in range(num_nodes * 2):
        visited.append(current_ip)

        cert = get_certificate(current_ip)

        next_ip = get_successor(current_ip)

        cert_display = (cert[:30] + '..') if len(cert) > 30 else cert
        print(f"{i+1:<5} | {current_ip:<15} -> {str(next_ip):<15} | {cert_display}")

        if not next_ip:
            print(f"\nCancel: Node {current_ip} is not responding.")
            return

        if next_ip == start_ip:
            print("-" * 75)
            print(f"Ring closed! Found {len(visited)} nodes.")
            return

        if next_ip in visited:
            print("-" * 75)
            print(f"Short circuit (Loop) found! Pointing to already found node {next_ip}.")
            return

        current_ip = next_ip

    print(f"\nRing not closed or too many moves needed (Limit: {num_nodes * 2}).")

if __name__ == "__main__":
    MASTER_IP = sys.argv[1]

    if len(sys.argv) > 2:
        NUM_NODES = int(sys.argv[2])
    else:
        NUM_NODES = 10

    walk_ring(MASTER_IP, NUM_NODES)