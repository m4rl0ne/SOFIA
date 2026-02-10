import os
import hashlib
import requests
import logging
from flask import Flask, request, jsonify
from apscheduler.schedulers.background import BackgroundScheduler
import threading
import time

# --- Logging Configuration ---
logging.basicConfig(level=logging.INFO, format='%(asctime)s | %(message)s', datefmt='%H:%M:%S')
logger = logging.getLogger(__name__)

# Reduce noise from Werkzeug (Flask)
log_werkzeug = logging.getLogger('werkzeug')
log_werkzeug.setLevel(logging.ERROR)

# Reduce noise from urllib3 (Requests)
logging.getLogger('urllib3').setLevel(logging.WARNING)

# Reduce noise from APScheduler
logging.getLogger('apscheduler').setLevel(logging.WARNING)

M_BIT = 160  # SHA-1 bit length
RING_SIZE = 2 ** M_BIT

# --- Helper functions ---
def get_sha1(key):
    """Generates SHA-1 hash of given string."""
    return int(hashlib.sha1(key.encode()).hexdigest(), 16)


def in_interval(id, start, end, right_inclusive=False):
    """
    Checks if id is in interval (start, end) or (start, end].
    Handles the modulo overflow at the end of the ring.
    """
    if start == end:  # Ring with only one node
        return True

    start = start % RING_SIZE
    end = end % RING_SIZE
    id = id % RING_SIZE

    if start < end:
        if right_inclusive:
            return start < id <= end
        else:
            return start < id < end
    else:  # Overflow (e.g., start=RingEnd, end=RingStart)
        if right_inclusive:
            return start < id or id <= end
        else:
            return start < id or id < end


# --- Chord Node ---
class ChordNode:
    def __init__(self, host, port=5000):
        self.host = host
        self.port = port
        self.url = f"http://{host}:{port}"
        self.id = get_sha1(host)

        # State
        self.predecessor = None
        self.successor = {"id": self.id, "host": self.host}  # Initially, I am my own ring

        # Finger Table: List of {"id": int, "host": str}
        # finger[i] is the successor of (self.id + 2^i)
        self.finger = [self.successor] * M_BIT
        self.next_finger_to_fix = 0

        # Storage (for X.509 certificates)
        self.local_storage = {}

        logger.info(f"Node init: {self.host} (ID: {self.id})")

    # --- Core Chord Logic ---
    def find_successor(self, id):
        """Finds the successor for a specific ID (Key or Node ID)."""
        # Case 1: ID is between me and my direct successor
        if in_interval(id, self.id, self.successor['id'], right_inclusive=True):
            return self.successor

        # Case 2: ID is further away -> Search for the next possible node in Finger Table
        n_prime = self.closest_preceding_node(id)

        # If we don't find a closer node (n_prime == self), we are likely
        # at the limit of our knowledge or the ring is small.
        if n_prime['id'] == self.id:
            return self.successor

        try:
            resp = requests.get(f"http://{n_prime['host']}:5000/api/find_successor?id={id}", timeout=1)
            if resp.status_code == 200:
                return resp.json()
        except:
            pass

        return self.successor

    def closest_preceding_node(self, id):
        """Searches the Finger Table for the node that is closest PRECEDING 'id'."""
        # We iterate backwards through the table (from furthest jump to shortest)
        for i in range(M_BIT - 1, -1, -1):
            finger_node = self.finger[i]
            if not finger_node: continue

            # Check if Finger[i] is in the interval (self, id)
            if in_interval(finger_node['id'], self.id, id):
                # Check if the node is reachable, otherwise skip it
                return finger_node
        return {"id": self.id, "host": self.host}

    def join(self, bootstrap_host):
        """Joins a ring by asking an existing node."""
        logger.info(f"Attempting join via {bootstrap_host}...")
        try:
            resp = requests.get(f"http://{bootstrap_host}:5000/api/find_successor?id={self.id}")
            if resp.status_code == 200:
                self.successor = resp.json()
                self.finger[0] = self.successor
                logger.info(f"Join successful! My successor is {self.successor['host']}")
            else:
                logger.error("Join failed: Bootstrap node did not respond correctly.")
        except Exception as e:
            logger.error(f"Join Error: {e}")

    def stabilize(self):
        """Checks successor and repairs the ring upon failure."""
        try:
            resp = requests.get(f"http://{self.successor['host']}:5000/api/get_predecessor", timeout=1.0)

            # If we are here, the successor is alive
            x = resp.json()

            # Check if the successor has a new predecessor that is closer to me
            if x and in_interval(x['id'], self.id, self.successor['id']):
                self.successor = x
                self.finger[0] = x

            requests.post(f"http://{self.successor['host']}:5000/api/notify",
                          json={"id": self.id, "host": self.host},
                          timeout=1.0)

        except Exception as e:
            logger.warning(f"!!! Successor {self.successor['host']} is not responding. Starting repair...")
            self.handle_successor_failure()

    def handle_successor_failure(self):
        """Searches in the Finger Table for a living node."""
        found_new_successor = False

        for finger_node in self.finger:
            if not finger_node: continue
            if finger_node['host'] == self.successor['host']: continue
            if finger_node['id'] == self.id: continue  # Do not ask myself

            # Is this finger reachable?
            try:
                check = requests.get(f"http://{finger_node['host']}:5000/api/ping", timeout=0.5)
                if check.status_code == 200:
                    logger.info(f"--> HEALING: Found new living successor: {finger_node['host']}")
                    self.successor = finger_node
                    self.finger[0] = finger_node
                    found_new_successor = True
                    break
            except:
                pass  # This one is dead too, keep searching...

        if not found_new_successor:
            logger.error("No alternative successor found! Ring is broken.")

    def notify(self, potential_predecessor):
        """Called by another node that believes it is my predecessor."""
        # Update if: I have no predecessor OR the new one is closer to me than the old one
        if (self.predecessor is None) or \
                in_interval(potential_predecessor['id'], self.predecessor['id'], self.id):
            self.predecessor = potential_predecessor
            logger.info(f"New Predecessor: {self.predecessor['host']}")

    def fix_fingers(self):
        """Periodically updates a random or sequential entry in the Finger Table."""
        self.next_finger_to_fix = (self.next_finger_to_fix + 1) % M_BIT
        i = self.next_finger_to_fix

        # Calculate ID: (self.id + 2^i) mod RING_SIZE
        target_id = (self.id + (2 ** i)) % RING_SIZE

        try:
            node = self.find_successor(target_id)
            self.finger[i] = node
        except Exception as e:
            pass

    def check_predecessor(self):
        """Checks if the predecessor is still alive."""
        if self.predecessor:
            try:
                requests.get(f"http://{self.predecessor['host']}:5000/api/ping", timeout=1)
            except:
                logger.warning(f"Predecessor {self.predecessor['host']} is dead.")
                self.predecessor = None

app = Flask(__name__)
node = ChordNode(os.environ.get('HOSTNAME', 'localhost'))

scheduler = BackgroundScheduler()
scheduler.add_job(func=node.stabilize, trigger="interval", seconds=1.0)
scheduler.add_job(func=node.fix_fingers, trigger="interval", seconds=0.5)
scheduler.add_job(func=node.check_predecessor, trigger="interval", seconds=3.0)
scheduler.start()

# --- API Endpoints ---
@app.route('/api/find_successor', methods=['GET'])
def api_find_successor():
    id = int(request.args.get('id'))
    return jsonify(node.find_successor(id))

@app.route('/api/get_predecessor', methods=['GET'])
def api_get_predecessor():
    return jsonify(node.predecessor)

@app.route('/api/notify', methods=['POST'])
def api_notify():
    node.notify(request.json)
    return jsonify({"status": "ok"})

@app.route('/api/ping', methods=['GET'])
def api_ping():
    return "pong", 200

@app.route('/storage/upload', methods=['POST'])
def upload_file():
    """Client uploads file. We find the owner and forward."""
    key = request.form.get('key')  # e.g., CommonName
    content = request.form.get('content')  # PEM Content

    key_id = get_sha1(key)
    logger.info(f"Client upload request: {key} (ID: {key_id})")

    owner = node.find_successor(key_id)

    if owner['id'] == node.id:
        logger.info(f"I ({node.host}) am responsible. Storing locally.")
        node.local_storage[str(key_id)] = content
        return jsonify({"status": "stored", "node": node.host, "id": node.id})

    else:
        logger.info(f"I am not responsible. Forwarding to {owner['host']}")
        try:
            resp = requests.post(f"http://{owner['host']}:5000/storage/internal_store", json={
                "key_id": str(key_id),
                "content": content
            })
            return jsonify(resp.json())
        except Exception as e:
            return jsonify({"error": str(e)}), 500


@app.route('/storage/internal_store', methods=['POST'])
def internal_store():
    """Called by other nodes to store data here."""
    data = request.json
    node.local_storage[data['key_id']] = data['content']
    logger.info(f"INTERNAL STORE: Stored data for ID {data['key_id']}.")
    return jsonify({"status": "stored", "node": node.host, "via": "routing"})


@app.route('/storage/retrieve', methods=['GET'])
def retrieve_file():
    """Client wants to retrieve data."""
    key = request.args.get('key')

    if not key:
        return jsonify({"found": False, "error": "Key not found."}), 404

    key_id = get_sha1(key)
    owner = node.find_successor(key_id)

    if owner['id'] == node.id:
        content = node.local_storage.get(str(key_id))
        if content:
            return jsonify({"found": True, "content": content, "node": node.host})
        else:
            return jsonify({"found": False, "error": "Not found locally"}), 404

    else:
        return requests.get(f"http://{owner['host']}:5000/storage/retrieve?key={key}").content

@app.route('/info', methods=['GET'])
def info():
    return jsonify({
        "node": node.host,
        "id": node.id,
        "successor": node.successor,
        "predecessor": node.predecessor,
        # Show only the first 5 fingers as an example
        "finger_sample": node.finger[:5],
        "storage_count": len(node.local_storage)
    })

# If a BOOTSTRAP_HOST is set, we try to join
bootstrap_host = os.environ.get('BOOTSTRAP_HOST')

if __name__ == '__main__':
    def initial_join():
        time.sleep(2)
        if bootstrap_host:
            node.join(bootstrap_host)

    if bootstrap_host:
        threading.Thread(target=initial_join).start()

    app.run(host='0.0.0.0', port=5000)