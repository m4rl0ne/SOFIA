import hashlib
import requests
import time

# Configuration
NODES = [
    "http://localhost:5001",
    "http://localhost:5002",
    "http://localhost:5003",
    "http://localhost:5004"
]
NODE_NAMES = ["node-1", "node-2", "node-3", "node-4"]

def get_sha1(key):
    return int(hashlib.sha1(key.encode()).hexdigest(), 16)

def print_separator():
    print("-" * 60)

print_separator()
print("1. TOPOLOGY CALCULATION (Prediction)")
print_separator()

# We get the real IDs (although we could calculate them locally)
node_info_list = []
for url in NODES:
    try:
        info = requests.get(f"{url}/info").json()
        node_info_list.append(info)
        print(f"Node: {info['node']} | ID: {str(info['id'])[:10]}...")
    except:
        print(f"WARNING: {url} unreachable.")

# Sort by ID to visualize the ring
node_info_list.sort(key=lambda x: x['id'])
print("\nRing Order:")
for n in node_info_list:
    print(f" -> {n['node']}")

print_separator()
print("2. FILE UPLOAD SIMULATION")
print_separator()

# A fictitious Key (e.g., the Common Name of a certificate)
filename = "my-secure-server.com"
file_content = "-----BEGIN CERTIFICATE-----\nMIIFyTCCA...\n-----END CERTIFICATE-----"
file_id = get_sha1(filename)

print(f"File: '{filename}'")
print(f"Hash ID: {file_id}")

# Prediction algorithm: Who is responsible?
# The first node whose ID >= file_id.
target_node = None
for node in node_info_list:
    if node['id'] >= file_id:
        target_node = node
        break

# If the File ID is larger than all Node IDs, it belongs to the first node (Ring wrap-around)
if target_node is None:
    target_node = node_info_list[0]

print(f"\nPREDICTION: The file must land on >> {target_node['node']} <<.")
print_separator()

# Upload to a *different* node (to test routing)
upload_node_url = NODES[0]  # We just use localhost:5001 (node-1)
print(f"Sending upload request to random node: {upload_node_url} ...")
try:
    resp = requests.post(f"{upload_node_url}/storage/upload", data={
        "key": filename,
        "content": file_content
    })

    # NEW: First check if everything is okay
    if resp.status_code == 200:
        print("Server Response:", resp.json())
    else:
        print(f"ATTENTION ERROR! Status Code: {resp.status_code}")
        print("Server Response (Text):", resp.text)  # Shows us the real error message

except Exception as e:
    print("Upload Exception:", e)

print_separator()
print("3. VERIFICATION (Where is it really?)")
print_separator()

time.sleep(1)  # Wait briefly for asynchronous processing

for url in NODES:
    info = requests.get(f"{url}/info").json()
    stored_items = info.get('storage_items', [])

    has_item = str(file_id) in stored_items
    marker = "   <--- HERE IT IS!" if has_item else ""
    print(f"{info['node']}: {len(stored_items)} items stored.{marker}")

print_separator()
print("4. DOWNLOAD TEST")
print_separator()

# We ask Node-3 for the file (even if it lies on Node-1)
download_url = NODES[2]
print(f"Asking {download_url} for '{filename}'...")
try:
    r = requests.get(f"{download_url}/storage/retrieve?key={filename}")
    if r.status_code == 200:
        data = r.json()
        print(f"SUCCESS! File found on Node: {data['node']}")
        print(f"Content Check: {data['content'][:20]}...")
    else:
        print("Error during download:", r.text)
except Exception as e:
    print(e)