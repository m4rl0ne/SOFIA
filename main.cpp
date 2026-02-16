#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#define CHORD_PORT 5000
#define DISCOVERY_PORT 5001

#include "Protocol.h"
#include "ChordNode.hpp"

std::atomic<bool> g_running(true);
void signalHandler(int) { g_running = false; }

#pragma pack(push, 1)
struct DiscoveryPacket {
    uint32_t magic;
    uint32_t sender_id;
};
#pragma pack(pop)

#define DISCOVERY_MAGIC 0x50434844

uint32_t get_local_ip() {
    uint32_t my_ip = 0;
#ifdef _WIN32
    char name[255];
    if (gethostname(name, sizeof(name)) == 0) {
        struct addrinfo hints, *res;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        if (getaddrinfo(name, NULL, &hints, &res) == 0) {
            my_ip = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
            freeaddrinfo(res);
        }
    }
#else
    struct ifaddrs *ifAddrStruct = nullptr;
    if (getifaddrs(&ifAddrStruct) != -1) {
        for (struct ifaddrs* ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (std::strcmp(ifa->ifa_name, "lo") != 0) { // lo ignorieren
                my_ip = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
                break;
            }
        }
        freeifaddrs(ifAddrStruct);
    }
#endif
    return my_ip;
}

void discovery_responder_thread(uint32_t my_id) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {AF_INET, htons(DISCOVERY_PORT), {INADDR_ANY}};
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return;

    while (g_running) {
        DiscoveryPacket incoming;
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        int n = recvfrom(sock, (char*)&incoming, sizeof(incoming), 0, (struct sockaddr*)&client_addr, &len);

        if (n == sizeof(DiscoveryPacket) && incoming.magic == DISCOVERY_MAGIC) {
            if (incoming.sender_id != my_id) {
                DiscoveryPacket reply = {DISCOVERY_MAGIC, my_id};
                sendto(sock, (const char*)&reply, sizeof(reply), 0, (struct sockaddr*)&client_addr, len);
            }
        }
    }
    closesocket(sock);
}

uint32_t discover_neighbor_ip(uint32_t my_id) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast_opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_opt, sizeof(broadcast_opt));

    timeval tv = {0, 500000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

    sockaddr_in b_addr = {AF_INET, htons(DISCOVERY_PORT)};
    b_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    DiscoveryPacket packet = {DISCOVERY_MAGIC, my_id};

    uint32_t found_ip = 0;

    sendto(sock, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&b_addr, sizeof(b_addr));

    sockaddr_in resp_addr;
    socklen_t resp_len = sizeof(resp_addr);
    DiscoveryPacket recv_pkt;

    int n = recvfrom(sock, (char*)&recv_pkt, sizeof(recv_pkt), 0, (struct sockaddr*)&resp_addr, &resp_len);

    if (n == sizeof(DiscoveryPacket)) {
        if (recv_pkt.magic == DISCOVERY_MAGIC) {
            if (recv_pkt.sender_id != my_id) {
                found_ip = resp_addr.sin_addr.s_addr;
                std::cout << "[DISCOVERY] Real neighbor found at " << inet_ntoa(resp_addr.sin_addr) << std::endl;
            } else {
                std::cout << "[DISCOVERY] Loopback detected (my own ID " << my_id << "). Ignoring..." << std::endl;
            }
        }
    }

    closesocket(sock);
    return found_ip;
}

void sendPacket(SOCKET sock, uint8_t type, const void* payload, uint32_t len) {
    PacketHeader hdr;
    hdr.magic = 0xCC;
    hdr.type = type;
    hdr.payload_len = len;
    send(sock, (const char*)&hdr, sizeof(hdr), 0);
    if (len > 0) send(sock, (const char*)payload, len, 0);
}

bool sendRpc(NodeInfo target, uint8_t type, const void* payload, uint32_t len, PacketHeader* out_hdr, uint8_t* out_buffer, uint32_t max_buffer_len, uint16_t timeout_ms = 200) {
    if (out_buffer && max_buffer_len > 0) std::memset(out_buffer, 0, max_buffer_len);
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv; tv.tv_sec = timeout_ms / 1000; tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = target.ip;
    addr.sin_port = htons(target.port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        sendPacket(sock, type, payload, len);
        if (out_hdr) {
            if (recv(sock, (char*)out_hdr, sizeof(PacketHeader), 0) == sizeof(PacketHeader)) {
                if (out_hdr->payload_len > 0 && out_buffer) {
                    uint32_t to_read = std::min(out_hdr->payload_len, max_buffer_len);
                    uint32_t received = 0;
                    while (received < to_read) {
                        int r = recv(sock, (char*)out_buffer + received, to_read - received, 0);
                        if (r <= 0) break;
                        received += r;
                    }
                }
                closesocket(sock); return true;
            }
        } else { closesocket(sock); return true; }
    }
    closesocket(sock); return false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    uint16_t discovery_port = 5001;
    uint16_t fixed_port = 5000;
    uint32_t bootstrap_ip = 0;

    uint32_t my_ip = get_local_ip();
    if (my_ip == 0) {
        std::cerr << "[ERROR] Could not determine local IP. Loopback fallback." << std::endl;
        my_ip = inet_addr("127.0.0.1");
    }

    std::srand(std::time(0) ^ my_ip);
    uint32_t my_discovery_id = std::rand();

    std::thread responder(discovery_responder_thread, my_discovery_id);
    responder.detach();

    std::srand(my_ip);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::rand() % 2000));

    if (argc > 1) {
        bootstrap_ip = inet_addr(argv[1]);
    } else {
        std::cout << "[DISCOVERY] Searching for neighbors via Broadcast..." << std::endl;
        bootstrap_ip = discover_neighbor_ip(my_discovery_id);
    }

    ChordNode node(my_ip, CHORD_PORT);

    if (bootstrap_ip == 0) {
        std::cout << "[SYSTEM] No neighbor found. I am the first node (Master)." << std::endl;
        const char* root_secret = "TRUST-ME-I-AM-ROOT";
        node.setCertificate((uint8_t*)root_secret, strlen(root_secret) + 1);
    } else {
        std::cout << "[SYSTEM] Found neighbor at " << inet_ntoa(*(in_addr*)&bootstrap_ip) << std::endl;
    }


    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in srv_addr;
    std::memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(fixed_port);
    srv_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    if (bind(server_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) == SOCKET_ERROR) return 1;
    listen(server_fd, 10);

#ifdef _WIN32
    u_long mode = 1; ioctlsocket(server_fd, FIONBIO, &mode);
#else
    fcntl(server_fd, F_SETFL, O_NONBLOCK);
#endif

    uint8_t rpc_buffer[4096];
    auto last_stabilize = std::chrono::steady_clock::now();
    auto last_join_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    while (g_running) {
        fd_set readfds; FD_ZERO(&readfds); FD_SET(server_fd, &readfds);
        timeval tv = {0, 20000};
        if (select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0) {
            sockaddr_in c_addr; socklen_t c_len = sizeof(c_addr);
            SOCKET client = accept(server_fd, (struct sockaddr*)&c_addr, &c_len);
            if (client != INVALID_SOCKET) {
                PacketHeader hdr;
                if (recv(client, (char*)&hdr, sizeof(hdr), 0) == sizeof(hdr)) {
                    std::vector<uint8_t> buf(hdr.payload_len);
                    if (hdr.payload_len > 0) {
                        uint32_t r = 0;
                        while(r < hdr.payload_len) {
                            int chunk = recv(client, (char*)buf.data()+r, hdr.payload_len-r, 0);
                            if (chunk <= 0) break;
                            r += chunk;
                        }
                    }

                    if (hdr.type == MSG_FIND_SUCCESSOR) {
                        FindSuccessorPayload* req = (FindSuccessorPayload*)buf.data();
                        NodeInfo next = node.findSuccessorNextHop(req->target_id);
                        NodeInfoPayload resp; resp.node = next;
                        sendPacket(client, MSG_FIND_SUCCESSOR_RESPONSE, &resp, sizeof(resp));
                    }
                    else if (hdr.type == MSG_GET_PREDECESSOR) {
                        if (node.hasPredecessor()) {
                            NodeInfoPayload resp; resp.node = node.getPredecessor();
                            sendPacket(client, MSG_GET_PREDECESSOR_RESPONSE, &resp, sizeof(resp));
                        } else {
                            PacketHeader err = hdr; err.payload_len = 0;
                            send(client, (char*)&err, sizeof(err), 0);
                        }
                    }
                    else if (hdr.type == MSG_NOTIFY) {
                        node.handleNotify(((NodeInfoPayload*)buf.data())->node);
                    }
                    else if (hdr.type == MSG_GET_SUCLIST) {
                        NodeListPayload resp;
                        node.getMySuccessorList(resp.nodes, &resp.count);
                        sendPacket(client, MSG_GET_SUCLIST_RESPONSE, &resp, sizeof(resp));
                    }
                    else if (hdr.type == MSG_GET_CERT) {
                        CertPayload resp; resp.cert_len = node.getCertLen();
                        std::memcpy(resp.data, node.getCertData(), resp.cert_len);
                        sendPacket(client, MSG_CERT_RESPONSE, &resp, sizeof(uint32_t) + resp.cert_len);
                    }
                    else if (hdr.type == MSG_PING) {
                        PacketHeader p_resp = hdr; p_resp.payload_len = 0;
                        send(client, (char*)&p_resp, sizeof(p_resp), 0);
                    }
                }
                closesocket(client);
            }
        }

        auto now = std::chrono::steady_clock::now();

        // --- 2. JOIN LOGIC (IP BASED) ---
        if (node.getSuccessor().ip == my_ip && (bootstrap_ip != 0 && bootstrap_ip != INADDR_NONE)) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_join_attempt).count() > 2) {
                NodeInfo target; target.ip = bootstrap_ip; target.port = fixed_port;
                FindSuccessorPayload req; req.target_id = node.getMyself().id;
                PacketHeader res_hdr;
                if (sendRpc(target, MSG_FIND_SUCCESSOR, &req, sizeof(req), &res_hdr, rpc_buffer, 4096, 1000)) {
                    if (res_hdr.type == MSG_FIND_SUCCESSOR_RESPONSE) {
                        NodeInfo suc = ((NodeInfoPayload*)rpc_buffer)->node;
                        node.setSuccessor(suc);
                        std::cout << "[JOIN] Successor found: " << inet_ntoa(*(in_addr*)&suc.ip) << std::endl;

                        if (sendRpc(suc, MSG_GET_CERT, nullptr, 0, &res_hdr, rpc_buffer, 4096, 500)) {
                            if (res_hdr.type == MSG_CERT_RESPONSE) {
                                CertPayload* cp = (CertPayload*)rpc_buffer;
                                node.setCertificate(cp->data, cp->cert_len);
                                std::cout << "[SECURITY] Certificate received." << std::endl;
                            }
                        }
                    }
                }
                last_join_attempt = now;
            }
        }

        // --- 3. STABILIZE LOGIC ---
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stabilize).count() > 200) {
            NodeInfo suc = node.getSuccessor();
            if (suc.ip != my_ip) {
                PacketHeader h;
                if (sendRpc(suc, MSG_GET_PREDECESSOR, nullptr, 0, &h, rpc_buffer, 4096)) {
                    if (h.type == MSG_GET_PREDECESSOR_RESPONSE && h.payload_len >= sizeof(NodeInfoPayload)) {
                        node.handleStabilizeResponse(((NodeInfoPayload*)rpc_buffer)->node);
                    }
                    if (sendRpc(suc, MSG_GET_SUCLIST, nullptr, 0, &h, rpc_buffer, 4096)) {
                        if (h.type == MSG_GET_SUCLIST_RESPONSE) {
                            NodeListPayload* lp = (NodeListPayload*)rpc_buffer;
                            node.updateSuccessorList(lp->nodes, lp->count);
                        }
                    }
                    NodeInfoPayload me; me.node = node.getMyself();
                    sendRpc(suc, MSG_NOTIFY, &me, sizeof(me), nullptr, nullptr, 200);
                } else {
                    node.handleSuccessorFailure();
                }
            } else if (node.hasPredecessor() && node.getPredecessor().ip != my_ip) {
                node.setSuccessor(node.getPredecessor());
            }
            last_stabilize = now;
        }
    }

    closesocket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    responder.join();
    return 0;
}