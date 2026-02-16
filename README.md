# SOFIA
## <u><b>S</b></u>ecure <u><b>O</b></u>verlay-Network <u><b>F</b></u>or <u><b>I</b></u>ndustrial <u><b>A</b></u>pplications

A **Proof-of-Concept (PoC)** implementation of a decentralized storage architecture based on the **Chord Protocol** (Distributed Hash Table) written in C++.

This project demonstrates how X.509 certificates (or arbitrary data) can be stored and retrieved in a Peer-to-Peer network without a central server. The architecture is fully containerized using **Docker** and implements **self-healing mechanisms** to handle node failures automatically.

## 🚀 How it works
### Dynamic Discovery (UDP Broadcast)
Unlike traditional Chord implementations that require a known bootstrap IP, this system features zero-configuration discovery. Nodes utilize UDP Broadcast (Port 5001) to find peers within the local network.
- Self-Echo Suppression: To prevent a node from "finding itself" in the same container, discovery packets include a unique Nonce (sender_id).
- Master Election: If no neighbors respond after multiple attempts, the node automatically promotes itself to a "Master" state to initialize the ring and generate the root credentials.

### Ring Topology & Finger Tables
The network maintains a circular ID space using SHA1 hashing.
- Routing: While the Successor and Predecessor maintain the immediate ring structure, Finger Tables allow for accelerated routing. Currently, finger tables do not point exponential distances away, so no O(logN) performance yet... **TBI**!
- Self-Healing: A periodic Stabilization algorithm ensures the ring remains intact even if nodes crash. Each node maintains a Successor List to provide fault tolerance against multiple simultaneous node failures.

### Industrial Security & Certificate Distribution
The primary goal of this DHT is the decentralized distribution of X.509 Certificates.
- Chain of Trust: Once the ring is formed, certificates are synchronized across nodes. This allows PLCs to verify the identity of their neighbors without a central Certificate Authority (CA) being online at all times.
- TLS Readiness: These certificates serve as the foundation for upgrading the raw TCP connections to secure TLS tunnels for industrial data exchange.

### Memory & Real-Time Optimization
Designed for embedded systems, the core logic avoids heap allocation (no std::vector in critical paths). By using fixed-size buffers and static memory structures, the system ensures deterministic behavior and high reliability on PLC hardware.

## 🚀 How to start the cluster:
The demo is dockerized, so you can start the docker cluster with 10 nodes with a single command, simulating 10 PLCs.

1. Clone the repository, `cd SOFIA`
2. Run `docker compose up --build` to start the ring, observe the console output. One node should be the master node. If you want more or less nodes, just add `--scale sps=5` with the number of nodes you want.
3. In a second terminal, run `python docker_ring_check.py START_IP NUM_NODES`, where `START_IP` is the IP of the first node in the docker network and `NUM_NODES` is the number of nodes you are expecting, default is 10. I.e. `python docker_ring_check.py 172.20.0.2 10`
4. Check if all nodes point to a successor and the ring is closed.
