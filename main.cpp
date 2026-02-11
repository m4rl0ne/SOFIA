#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

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
// Dies simuliert, was auf der SPS im Hintergrund passieren würde
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

        // 1. Wir fragen den Bootstrap-Node: "Wer ist Successor für MEINE ID?"
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

    while (true) {
        // A. Eingehende Verbindungen bearbeiten
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock != INVALID_SOCKET) {
            PacketHeader hdr;
            if (recv(client_sock, (char*)&hdr, sizeof(hdr), 0) == sizeof(hdr)) {

                std::vector<uint8_t> buf(hdr.payload_len);
                if (hdr.payload_len > 0) recv(client_sock, (char*)buf.data(), hdr.payload_len, 0);

                // --- MESSAGE HANDLER ---
                if (hdr.type == MSG_FIND_SUCCESSOR) {
                    FindSuccessorPayload* req = (FindSuccessorPayload*)buf.data();
                    // Logik fragen: Wer ist zuständig?
                    NodeInfo next_hop = node.findSuccessorNextHop(req->target_id);

                    // Antwort senden
                    NodeInfoPayload resp; resp.node = next_hop;
                    sendPacket(client_sock, MSG_FIND_SUCCESSOR_RESPONSE, &resp, sizeof(resp));
                }
                else if (hdr.type == MSG_GET_PREDECESSOR) {
                    // Jemand fragt nach meinem Predecessor (für Stabilize)
                    NodeInfoPayload resp;
                    if (node.hasPredecessor()) {
                        resp.node = node.getPredecessor();
                        sendPacket(client_sock, MSG_GET_PREDECESSOR_RESPONSE, &resp, sizeof(resp));
                    } else {
                        // Sende leeres Paket oder Magic Error, hier einfach Socket zu -> Client merkt es
                    }
                }
                else if (hdr.type == MSG_NOTIFY) {
                    // Jemand sagt er ist mein Predecessor
                    NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                    node.handleNotify(p->node);
                }
            }
            closesocket(client_sock);
        }

        // B. Periodische Tasks (Stabilize) - z.B. alle 1 Sekunde
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stabilize).count() > 1000) {

            // 1. Stabilize: Frage Successor nach SEINEM Predecessor
            NodeInfo suc = node.getSuccessor();
            // Nicht mich selbst fragen (außer ich bin allein)
            if (suc.port != port) {
                PacketHeader resp_hdr = {0,0,0};
                std::vector<uint8_t> resp_data;

                // RPC: GET_PREDECESSOR
                sendRpc(suc, MSG_GET_PREDECESSOR, nullptr, 0, &resp_hdr, &resp_data);

                if (resp_hdr.type == MSG_GET_PREDECESSOR_RESPONSE) {
                    NodeInfoPayload* p = (NodeInfoPayload*)resp_data.data();
                    node.handleStabilizeResponse(p->node);
                }

                // 2. Notify: Sag dem Successor, dass ich da bin
                NodeInfoPayload my_info; my_info.node = node.getMyself();
                sendRpc(node.getSuccessor(), MSG_NOTIFY, &my_info, sizeof(my_info), nullptr, nullptr);
            }

            last_stabilize = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    closesocket(server_fd);
    return 0;
}