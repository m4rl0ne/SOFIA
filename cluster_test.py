import subprocess
import time
import socket
import struct
import sys
import atexit
import os

BINARY_PATH = "./build/chord_node"
START_PORT = 5000
NUM_NODES = 10
BOOTSTRAP_PORT = 5000

FMT_HEADER = '<B B I'
FMT_NODE_INFO = '<20s I H'

MSG_FIND_SUCCESSOR = 0x02
MSG_FIND_SUCCESSOR_RESPONSE = 0x03

# Prozess Liste
processes = []

def cleanup():
    print("\n[TEST] Fahre Cluster herunter...")
    for p in processes:
        if p.poll() is None:
            p.terminate()
            p.wait()
    print("[TEST] Done.")

atexit.register(cleanup)

def get_sha1_from_port(port):
    return port & 0xFF

def parse_node_info(data):
    try:
        node_id_bytes, ip, port = struct.unpack(FMT_NODE_INFO, data[:26])
        tiny_id = node_id_bytes[19]
        return {"id": tiny_id, "port": port, "ip": ip}
    except Exception as e:
        print(f"Error parsing: {e}")
        return None

def send_rpc_find_successor(target_port, search_id_byte):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1.0)
        sock.connect(('127.0.0.1', target_port))

        target_id = bytearray(20)
        target_id[19] = search_id_byte

        header = struct.pack(FMT_HEADER, 0xCC, MSG_FIND_SUCCESSOR, 20)

        sock.sendall(header + target_id)

        resp_hdr_bytes = sock.recv(6) # 1+1+4 bytes
        if len(resp_hdr_bytes) < 6: return None

        magic, msg_type, payload_len = struct.unpack(FMT_HEADER, resp_hdr_bytes)

        if msg_type == MSG_FIND_SUCCESSOR_RESPONSE:
            payload = sock.recv(payload_len)
            return parse_node_info(payload)

        sock.close()
    except Exception as e:
        pass
    return None

def start_cluster():
    print(f"[TEST] Starte {NUM_NODES} Nodes...")

    if not os.path.exists("logs"):
        os.makedirs("logs")

    print(f"  -> Starte Bootstrap Node auf Port {START_PORT}")
    with open(f"logs/node_{START_PORT}.log", "w") as log:
        p = subprocess.Popen([BINARY_PATH, str(START_PORT)], stdout=log, stderr=log)
        processes.append(p)

    time.sleep(1)

    for i in range(1, NUM_NODES):
        port = START_PORT + i
        print(f"  -> Starte Node {i} auf Port {port} (Join {BOOTSTRAP_PORT})")
        with open(f"logs/node_{port}.log", "w") as log:
            # ./chord_node [PORT] [BOOTSTRAP_PORT]
            p = subprocess.Popen([BINARY_PATH, str(port), str(BOOTSTRAP_PORT)], stdout=log, stderr=log)
            processes.append(p)
            time.sleep(0.2)

    print("[TEST] Cluster läuft. Warte 10s auf Stabilisierung...")
    time.sleep(10)

def print_topology():
    print("\n--- RING TOPOLOGIE CHECK ---")
    nodes_found = []

    for i in range(NUM_NODES):
        port = START_PORT + i
        my_id = get_sha1_from_port(port)

        search_id = (my_id + 1) % 256
        result = send_rpc_find_successor(port, search_id)

        if result:
            print(f"Node {port} (ID {my_id}) \t--> Successor: {result['port']} (ID {result['id']})")
            nodes_found.append(port)
        else:
            print(f"Node {port} (ID {my_id}) \t--> TOT / Timeout")

    return len(nodes_found)

def test_scenario():
    start_cluster()

    active = print_topology()
    if active == NUM_NODES:
        print("Initialer Ring ist vollständig.")
    else:
        print(f"Warnung: Nur {active}/{NUM_NODES} erreichbar.")

    kill_idx = 5
    kill_port = START_PORT + kill_idx
    print(f"\n[SCENARIO] TÖTE NODE auf Port {kill_port} (Simulation Absturz)...")

    processes[kill_idx].terminate()
    processes[kill_idx].wait()

    print("Warte 5s auf Failover / Heilung...")
    time.sleep(5)

    print_topology()

    check_node = kill_port - 1
    target_id = get_sha1_from_port(kill_port) # ID des toten Nodes

    print(f"\n[CHECK] Frage Node {check_node}, wer für ID {target_id} (Toter Node) zuständig ist...")
    res = send_rpc_find_successor(check_node, target_id)

    if res:
        print(f"Antwort: Port {res['port']} (ID {res['id']})")
        if res['port'] != kill_port:
            print("SUCCESS: Ring hat das Ziel umgeleitet (nicht mehr der tote Node).")
        else:
            print("FAIL: Zeigt immer noch auf den toten Node.")
    else:
        print("FAIL: Keine Antwort.")

    print("Warte 15 Sekunden für Updates.")
    time.sleep(30)
    print_topology()



if __name__ == "__main__":
    try:
        test_scenario()
        input("\nDrücke ENTER zum Beenden (Killt alle Nodes)...")
    except KeyboardInterrupt:
        pass