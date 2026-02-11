#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

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

// Hilfsfunktion zum Senden eines Pakets
void sendPacket(SOCKET sock, uint8_t type, const void* payload, uint32_t len) {
    PacketHeader hdr;
    hdr.magic = 0xCC; // Beispiel Magic Byte
    hdr.type = type;
    hdr.payload_len = len;

    send(sock, (const char*)&hdr, sizeof(hdr), 0);
    if (len > 0) {
        send(sock, (const char*)payload, len, 0);
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = DEFAULT_PORT;
    uint16_t bootstrap_port = 0;

    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) bootstrap_port = std::atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // --- NODE INIT ---
    // IP 127.0.0.1 (Localhost) für Test
    uint32_t my_ip = 0x7F000001;
    ChordNode node(my_ip, port);

    // --- SOCKET SETUP (SERVER) ---
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }
    listen(server_fd, 5);

    // Non-Blocking Mode setzen (wichtig für SPS-Simulation)
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(server_fd, FIONBIO, &mode);
#else
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
#endif

    std::cout << "[NET] Listening on port " << port << " (TCP Raw)" << std::endl;

    // --- BOOTSTRAP JOIN (CLIENT) ---
    if (bootstrap_port != 0) {
        std::cout << "[NET] Connecting to bootstrap node on port " << bootstrap_port << "..." << std::endl;
        SOCKET client_sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in target;
        target.sin_family = AF_INET;
        target.sin_addr.s_addr = inet_addr("127.0.0.1");
        target.sin_port = htons(bootstrap_port);

        if (connect(client_sock, (struct sockaddr*)&target, sizeof(target)) == 0) {
            // Sende Join Request (Payload ist leer oder meine eigene Info)
            NodeInfoPayload payload;
            payload.node = node.getMyself();
            sendPacket(client_sock, MSG_JOIN_REQ, &payload, sizeof(payload));

            // Auf Antwort warten (Blocking für simplicity hier im Startup)
            PacketHeader resp_hdr;
            recv(client_sock, (char*)&resp_hdr, sizeof(resp_hdr), 0);
            if (resp_hdr.type == MSG_FIND_SUCCESSOR_RESPONSE) {
                 NodeInfoPayload resp_payload;
                 recv(client_sock, (char*)&resp_payload, sizeof(resp_payload), 0);
                 node.handleJoinResponse(resp_payload.node);
            }
            closesocket(client_sock);
        } else {
            std::cerr << "Bootstrap failed!" << std::endl;
        }
    }

    // --- MAIN LOOP (Zyklus) ---
    while (true) {
        // 1. Accept new connections
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock != INVALID_SOCKET) {
            // Nachricht empfangen (Header)
            PacketHeader hdr;
            int n = recv(client_sock, (char*)&hdr, sizeof(hdr), 0);

            if (n == sizeof(PacketHeader)) {
                // Dispatcher
                if (hdr.type == MSG_JOIN_REQ) {
                    // Jemand will joinen. Einfachster Fall: Ich bin der Successor.
                    // Payload lesen (NodeInfo des Joiners)
                    NodeInfoPayload joiner;
                    recv(client_sock, (char*)&joiner, sizeof(joiner), 0);

                    std::cout << "[NET] Join Request from Port " << joiner.node.port << std::endl;

                    // Antwort: Ich bin dein Successor (vereinfacht)
                    NodeInfoPayload resp;
                    resp.node = node.getMyself(); // oder node.findSuccessorLogic(...)
                    sendPacket(client_sock, MSG_FIND_SUCCESSOR_RESPONSE, &resp, sizeof(resp));
                }
                // Hier weitere Handler für FIND_SUCCESSOR etc.
            }
            closesocket(client_sock);
        }

        // 2. Periodic Tasks
        node.stabilize();

        // 3. Sleep (Zykluszeit Simulation)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    closesocket(server_fd);
    return 0;
}