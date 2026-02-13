#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
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
void signalHandler(int) { g_running = false; }

void sendPacket(SOCKET sock, uint8_t type, const void* payload, uint32_t len) {
    PacketHeader hdr;
    hdr.magic = 0xCC;
    hdr.type = type;
    hdr.payload_len = len;
    send(sock, (const char*)&hdr, sizeof(hdr), 0);
    if (len > 0) send(sock, (const char*)payload, len, 0);
}

bool sendRpc(NodeInfo target, uint8_t type, const void* payload, uint32_t len, PacketHeader* out_hdr, uint8_t* out_buffer, uint32_t max_buffer_len, uint16_t timeout_ms = 200) {

    if (out_buffer && max_buffer_len > 0) {
        std::memset(out_buffer, 0, max_buffer_len);
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = target.ip;
    addr.sin_port = htons(target.port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        sendPacket(sock, type, payload, len);

        if (out_hdr) {
            int n = recv(sock, (char*)out_hdr, sizeof(PacketHeader), 0);
            if (n != sizeof(PacketHeader)) {
                closesocket(sock);
                return false;
            }

            if (out_hdr->payload_len > 0 && out_buffer) {
                uint32_t to_read = (out_hdr->payload_len < max_buffer_len) ? out_hdr->payload_len : max_buffer_len;
                uint32_t received = 0;
                while (received < to_read) {
                    int r = recv(sock, (char*)out_buffer + received, to_read - received, 0);
                    if (r <= 0) break;
                    received += r;
                }

                if (received < to_read) {
                    closesocket(sock);
                    return false;
                }
            }
        }
        closesocket(sock);
        return true;
    }
    closesocket(sock);
    return false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    uint16_t port = DEFAULT_PORT;
    uint16_t bootstrap_port = 0;
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) bootstrap_port = std::atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    uint32_t my_ip = inet_addr("127.0.0.1");
    ChordNode node(my_ip, port);

    // Server Socket Setup
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {AF_INET, htons(port), {0}};
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed on port " << port << std::endl;
        return 1;
    }
    listen(server_fd, 5);

    uint8_t rpc_buffer[4096];
    std::memset(rpc_buffer, 0, sizeof(rpc_buffer));

#ifdef _WIN32
    u_long mode = 1; ioctlsocket(server_fd, FIONBIO, &mode);
#else
    fcntl(server_fd, F_SETFL, O_NONBLOCK);
#endif

    auto last_stabilize = std::chrono::steady_clock::now();
    auto last_join_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    std::cout << "[SYSTEM] Node " << port << " started. Bootstrap: " << bootstrap_port << std::endl;

    // Add certificate for first node
    if (bootstrap_port == 0) {
        const char* root_secret = "TRUST-ME-I-AM-ROOT";
        node.setCertificate((uint8_t*)root_secret, strlen(root_secret));
        std::cout << "[SECURITY] Root-Node initialized with Master-Certificate." << std::endl;
    }

    while (g_running) {
        // --- 1. NETZWERK VERARBEITUNG (Non-Blocking Accept) ---
        fd_set readfds; FD_ZERO(&readfds); FD_SET(server_fd, &readfds);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 20000; // 20ms polling
        select(server_fd + 1, &readfds, NULL, NULL, &tv);

        if (FD_ISSET(server_fd, &readfds)) {
            sockaddr_in c_addr; socklen_t c_len = sizeof(c_addr);
            SOCKET client = accept(server_fd, (struct sockaddr*)&c_addr, &c_len);
            if (client != INVALID_SOCKET) {
                PacketHeader hdr;
                if (recv(client, (char*)&hdr, sizeof(hdr), 0) == sizeof(hdr)) {
                    std::vector<uint8_t> buf(hdr.payload_len);
                    if (hdr.payload_len > 0) {
                        int r = 0;
                        while(r < (int)hdr.payload_len) {
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
                        NodeInfoPayload resp;
                        if (node.hasPredecessor()) {
                            resp.node = node.getPredecessor();
                            sendPacket(client, MSG_GET_PREDECESSOR_RESPONSE, &resp, sizeof(resp));
                        } else {
                            PacketHeader err = hdr; err.payload_len = 0;
                            send(client, (char*)&err, sizeof(err), 0);
                        }
                    }
                    else if (hdr.type == MSG_NOTIFY) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleNotify(p->node);
                    }
                    else if (hdr.type == MSG_GET_SUCLIST) {
                        NodeListPayload resp;
                        node.getMySuccessorList(resp.nodes, &resp.count);
                        sendPacket(client, MSG_GET_SUCLIST_RESPONSE, &resp, sizeof(resp));
                    }
                    else if (hdr.type == MSG_SET_SUCCESSOR) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleSetSuccessor(p->node);
                    }
                    else if (hdr.type == MSG_SET_PREDECESSOR) {
                        NodeInfoPayload* p = (NodeInfoPayload*)buf.data();
                        node.handleSetPredecessor(p->node);
                    }
                    else if (hdr.type == MSG_PING) {
                        PacketHeader resp = hdr;
                        resp.payload_len = 0;
                        send(client, (char*)&resp, sizeof(resp), 0);
                    }
					else if (hdr.type == MSG_GET_CERT) {
    					CertPayload resp;
    					resp.cert_len = node.getCertLen();
    					std::memcpy(resp.data, node.getCertData(), resp.cert_len);

					    sendPacket(client, MSG_CERT_RESPONSE, &resp, sizeof(uint32_t) + resp.cert_len);
					}
                }
                closesocket(client);
            }
        }

        auto now = std::chrono::steady_clock::now();

        // --- 2. JOIN LOGIC ---
        if (node.getSuccessor().port == port && bootstrap_port != 0) {

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_join_attempt).count() > 2000) {

                NodeInfo target; target.ip = my_ip; target.port = bootstrap_port;
                FindSuccessorPayload req; req.target_id = node.getMyself().id;
                PacketHeader resp_hdr;

                if (sendRpc(target, MSG_FIND_SUCCESSOR, &req, sizeof(req), &resp_hdr, rpc_buffer, 4096, 1000)) {

                    if (resp_hdr.type == MSG_FIND_SUCCESSOR_RESPONSE && resp_hdr.payload_len >= sizeof(NodeInfoPayload)) {

                        NodeInfoPayload* nip = (NodeInfoPayload*)rpc_buffer;
                        NodeInfo new_suc = nip->node;

                        node.setSuccessor(new_suc);
                        std::cout << "[JOIN] Joined ring. New successor: " << new_suc.port << std::endl;

                        PacketHeader cert_hdr;
                        if (sendRpc(new_suc, MSG_GET_CERT, nullptr, 0, &cert_hdr, rpc_buffer, 4096, 500)) {

                            if (cert_hdr.type == MSG_CERT_RESPONSE && cert_hdr.payload_len >= sizeof(uint32_t)) {
                                CertPayload* cp = (CertPayload*)rpc_buffer;

                                if (cp->cert_len > 0 && cp->cert_len <= 2048) {
                                    node.setCertificate(cp->data, cp->cert_len);
                                    std::cout << "[SECURITY] Certificate synchronized during join." << std::endl;
                                }
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

            if (suc.port == port) {
                if (node.hasPredecessor() && node.getPredecessor().port != port) {
                    node.setSuccessor(node.getPredecessor());
                }
            }
            else {
                PacketHeader resp_hdr;
                if (sendRpc(suc, MSG_GET_PREDECESSOR, nullptr, 0, &resp_hdr, rpc_buffer, 4096)) {

                    if (resp_hdr.type == MSG_GET_PREDECESSOR_RESPONSE && resp_hdr.payload_len >= sizeof(NodeInfoPayload)) {
                        NodeInfoPayload* p = (NodeInfoPayload*)rpc_buffer;
                        node.handleStabilizeResponse(p->node);
                    }

                    if (sendRpc(suc, MSG_GET_SUCLIST, nullptr, 0, &resp_hdr, rpc_buffer, 4096)) {
                        if (resp_hdr.type == MSG_GET_SUCLIST_RESPONSE && resp_hdr.payload_len >= sizeof(NodeListPayload)) {
                            NodeListPayload* lp = (NodeListPayload*)rpc_buffer;
                            node.updateSuccessorList(lp->nodes, lp->count);
                        }
                    }

                    NodeInfoPayload me; me.node = node.getMyself();
                    sendRpc(suc, MSG_NOTIFY, &me, sizeof(me), nullptr, nullptr, 200);

                } else {
                    node.handleSuccessorFailure();
                }
            }
            last_stabilize = now;
        }
    }

    closesocket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}