#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <csignal>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include "Protocol.h"
#include "ChordNode.hpp"

std::atomic<bool> g_running(true);

// Signal Handler (fängt Ctrl+C ab)
void signalHandler(int signum) {
    std::cout << "\n[SYSTEM] Interrupt signal (" << signum << ") received. Stopping..." << std::endl;
    g_running = false; // Schleife beenden
}

// Hilfsfunktion: Ein Paket senden
void sendPacket(SOCKET sock, uint8_t type, const void* payload, uint32_t len) {
    PacketHeader hdr;
    hdr.magic = 0xCC;
    hdr.type = type;
    hdr.payload_len = len;
    send(sock, (const char*)&hdr, sizeof(hdr), 0);
    if (len > 0) send(sock, (const char*)payload, len, 0);
}

// Hilfsfunktion: Kurzzeit-Verbindung zu anderem Node (RPC-Style)
void sendRpc(NodeInfo target, uint8_t type, const void* payload, uint32_t len, PacketHeader* out_hdr, std::vector<uint8_t>* out_data) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = target.ip; // IP ist schon network byte order
    addr.sin_port = htons(target.port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        sendPacket(sock, type, payload, len);

        // Optional: Auf Antwort warten (wenn out_hdr übergeben wurde)
        if (out_hdr) {
            recv(sock, (char*)out_hdr, sizeof(PacketHeader), 0);
            if (out_hdr->payload_len > 0 && out_data) {
                out_data->resize(out_hdr->payload_len);
                recv(sock, (char*)out_data->data(), out_hdr->payload_len, 0);
            }
        }
    }
    closesocket(sock);
}

int main(int argc, char* argv[]) {
    // --- SETUP ---
    signal(SIGINT, signalHandler);

    uint16_t port = DEFAULT_PORT;
    uint16_t bootstrap_port = 0;
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) bootstrap_port = std::atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // Localhost IP (127.0.0.1)
    uint32_t my_ip = inet_addr("127.0.0.1");
    ChordNode node(my_ip, port);

    // --- SERVER SOCKET ---
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {AF_INET, htons(port), {0}};
    addr.sin_addr.s_addr = INADDR_ANY;

    // SO_REUSEADDR erlaubt schnellen Neustart auf gleichem Port
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed on port " << port << std::endl; return 1;
    }
    listen(server_fd, 5);

    // Non-Blocking Mode
#ifdef _WIN32
    u_long mode = 1; ioctlsocket(server_fd, FIONBIO, &mode);
#else
    fcntl(server_fd, F_SETFL, O_NONBLOCK);
#endif

    // --- BOOTSTRAP (JOIN) ---
    if (bootstrap_port != 0) {
        std::cout << "[INIT] Bootstrapping via Port " << bootstrap_port << "..." << std::endl;

        NodeInfo target; target.ip = my_ip; target.port = bootstrap_port;
        FindSuccessorPayload req; req.target_id = node.getMyself().id;

        PacketHeader resp_hdr;
        std::vector<uint8_t> resp_data;
        sendRpc(target, MSG_FIND_SUCCESSOR, &req, sizeof(req), &resp_hdr, &resp_data);

        if (resp_hdr.type == MSG_FIND_SUCCESSOR_RESPONSE) {
            NodeInfoPayload* payload = (NodeInfoPayload*)resp_data.data();
            node.setSuccessor(payload->node);
        } else {
            std::cerr << "[ERROR] Bootstrap failed. No valid response." << std::endl;
        }
    }

    // --- MAIN LOOP ---
    auto last_stabilize = std::chrono::steady_clock::now();

    std::cout << "[SYSTEM] Node running. Press Ctrl+C to leave gracefully." << std::endl;

    while (g_running) {
        // Damit wir g_running regelmäßig prüfen können, auch wenn kein Paket kommt
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms Timeout

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            SOCKET client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

            if (client_sock != INVALID_SOCKET) {
                PacketHeader hdr;
                int n = recv(client_sock, (char*)&hdr, sizeof(hdr), 0);

                if (n == sizeof(PacketHeader)) {
                    std::vector<uint8_t> buf(hdr.payload_len);
                    if (hdr.payload_len > 0) recv(client_sock, (char*)buf.data(), hdr.payload_len, 0);

                    // --- MESSAGE DISPATCHER ---
                    if (hdr.type == MSG_FIND_SUCCESSOR) {
                        FindSuccessorPayload* req = (FindSuccessorPayload*)buf.data();
                        NodeInfo next_hop = node.findSuccessorNextHop(req->target_id);
                        NodeInfoPayload resp; resp.node = next_hop;
                        sendPacket(client_sock, MSG_FIND_SUCCESSOR_RESPONSE, &resp, sizeof(resp));
                    }
                    else if (hdr.type == MSG_GET_PREDECESSOR) {
                        NodeInfoPayload resp;
                        if (node.hasPredecessor()) {
                            resp.node = node.getPredecessor();
                            sendPacket(client_sock, MSG_GET_PREDECESSOR_RESPONSE, &resp, sizeof(resp));
                        } else {
                            // Sende leeres Paket (Header only) als "Kein Predecessor"
                            PacketHeader err_hdr = hdr;
                            err_hdr.payload_len = 0;
                            send(client_sock, (char*)&err_hdr, sizeof(err_hdr), 0);
                        }
                    }
                    else if (hdr.type == MSG_NOTIFY) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleNotify(p->node);
                    }
                    else if (hdr.type == MSG_SET_SUCCESSOR) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleSetSuccessor(p->node);
                    }
                    else if (hdr.type == MSG_SET_PREDECESSOR) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleSetPredecessor(p->node);
                    }
                }
                closesocket(client_sock);
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Timer: 100ms für den Test (sonst 1000ms)
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stabilize).count() > 100) {

            NodeInfo suc = node.getSuccessor();

            if (suc.port == port) {
                if (node.hasPredecessor()) {
                    NodeInfo pred = node.getPredecessor();
                    if (pred.port != port) {
                        node.setSuccessor(pred);
                    }
                }
            }

            else {
                PacketHeader resp_hdr;
                std::vector<uint8_t> resp_data;

                sendRpc(suc, MSG_GET_PREDECESSOR, nullptr, 0, &resp_hdr, &resp_data);

                if (resp_hdr.type == MSG_GET_PREDECESSOR_RESPONSE) {
                    NodeInfoPayload* p = (NodeInfoPayload*)resp_data.data();

                    // Das hier prüft, ob der Vorgänger von 5001 (z.B. 5000.5) besser zu mir passt als 5001.
                    node.handleStabilizeResponse(p->node);
                }

                // B. Notify: "Hallo Successor, ich bin dein Vorgänger!"
                NodeInfoPayload my_info;
                my_info.node = node.getMyself();
                sendRpc(suc, MSG_NOTIFY, &my_info, sizeof(my_info), nullptr, nullptr);
            }

            last_stabilize = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // --- GRACEFUL LEAVE PROTOKOLL ---
    std::cout << "\n[LEAVE] Starting graceful leave protocol..." << std::endl;

    NodeInfo S = node.getSuccessor();
    NodeInfo P = node.getPredecessor();
    bool hasP = node.hasPredecessor();

    if (hasP && P.port != node.getMyself().port) {
        std::cout << "[LEAVE] Telling Predecessor (Port " << P.port << ") -> New Successor is " << S.port << std::endl;
        NodeInfoPayload payload;
        payload.node = S;
        sendRpc(P, MSG_SET_SUCCESSOR, &payload, sizeof(payload), nullptr, nullptr);
    }

    if (S.port != node.getMyself().port && hasP) {
        std::cout << "[LEAVE] Telling Successor (Port " << S.port << ") -> New Predecessor is " << P.port << std::endl;
        NodeInfoPayload payload;
        payload.node = P;
        sendRpc(S, MSG_SET_PREDECESSOR, &payload, sizeof(payload), nullptr, nullptr);
    }
    else if (!hasP) {
        std::cout << "[LEAVE] I have no predecessor. Not updating Successor's predecessor." << std::endl;
    }

    std::cout << "[LEAVE] Bye bye!" << std::endl;
    closesocket(server_fd);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}