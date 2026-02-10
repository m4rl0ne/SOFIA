# SOFIA
## <u><b>S</b></u>ecure <u><b>O</b></u>verlay-Network <u><b>F</b></u>or <u><b>I</b></u>ndustrial <u><b>A</b></u>pplications

A **Proof-of-Concept (PoC)** implementation of a decentralized storage architecture based on the **Chord Protocol** (Distributed Hash Table).

This project demonstrates how X.509 certificates (or arbitrary data) can be stored and retrieved in a Peer-to-Peer network without a central server. The architecture is fully containerized using **Docker** and implements **self-healing mechanisms** to handle node failures automatically.

## 🚀 Features

* **Full Chord Logic:** Implementation of Ring Topology, Finger Tables ($O(\log N)$ routing), and Stabilization.
* **Consistent Hashing:** Automatic distribution of data based on SHA-1 hash values.
* **Decentralized Storage:** Files are automatically routed to the mathematically responsible node.
* **Fault Tolerance:** The ring detects failed nodes and repairs the routing path automatically.
* **REST API:** Simple HTTP interface for uploads, downloads, and debugging.
* **Docker Orchestration:** Automatic bootstrapping of the cluster via Docker Compose.
* **Smart Logging:** Reduced log noise, focusing on topology changes (Joins, Failovers).

## How to use
1) `docker compose up --build`
2) See logs in console: `node-1`, `node-2`, `node-3`, `node-4` are joining the ring. Wait 10-15 seconds for the ring to settle 
3) Start or stop nodes to see the ring healing: `docker compose stop node-2` or `docker compose start node-2`
