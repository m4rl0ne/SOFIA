#ifndef CHORDNODE_H
#define CHORDNODE_H

#include "Protocol.h"
#include <array>
#include <iostream>

class ChordNode {
public:
    ChordNode(uint32_t my_ip, uint16_t my_port) {
        myself.ip = my_ip;
        myself.port = my_port;

        std::memset(myself.id.bytes, 0, 20);
        std::memcpy(&myself.id.bytes[16], &my_ip, 4);

        for(int i = 0; i < SUCLIST_SIZE; ++i) {
            successor_list[i] = myself;
        }

        predecessor_valid = false;
		my_cert_len = 0;
		has_cert = false;
        std::cout << "[NODE] Init ID: " << (int)myself.id.toTinyID() << std::endl;
    }

    NodeInfo getSuccessor() const { return successor_list[0]; }
    NodeInfo getPredecessor() const { return predecessor; }
    bool hasPredecessor() const { return predecessor_valid; }
    NodeInfo getMyself() const { return myself; }

    void setSuccessor(const NodeInfo& new_suc) {
        for(int i=0; i<SUCLIST_SIZE; ++i) {
            successor_list[i] = new_suc;
        }
        std::cout << "[UPDATE] Successor set to " << new_suc.id << " (List Reset)" << std::endl;
    }

    void invalidatePredecessor() {
        predecessor_valid = false;
        std::memset(predecessor.id.bytes, 0, 20);
        predecessor.ip = 0;
    }

    void handleSuccessorFailure() {
        std::cout << "[FAILOVER] Successor " << successor_list[0].id << " unreachable!" << std::endl;

        for(int i=0; i < SUCLIST_SIZE-1; ++i) {
            successor_list[i] = successor_list[i+1];
        }
        successor_list[SUCLIST_SIZE-1] = myself;

        std::cout << "[FAILOVER] New Successor is " << successor_list[0].id << std::endl;

        invalidatePredecessor();
    }

    /**
    * At the moment no O(log N) implementation of the finger table.
    */
    void updateSuccessorList(const NodeInfo* received_list, int count) {
        bool changed = false;

        for(int i=0; i < count && i < (SUCLIST_SIZE-1); ++i) {
            if (successor_list[i+1].id != received_list[i].id) {
                successor_list[i+1] = received_list[i];
                changed = true;
            }
        }

        if (changed) {
            // std::cout << "[INFO] Backup-List updated." << std::endl;
        }
    }

    void getMySuccessorList(NodeInfo* out, uint8_t* out_count) {
        *out_count = SUCLIST_SIZE;
        for(int i=0; i<SUCLIST_SIZE; ++i) out[i] = successor_list[i];
    }

    void handleStabilizeResponse(const NodeInfo& x) {
        if (in_interval(x.id, myself.id, successor_list[0].id)) {

            if (x.id == successor_list[0].id) return;

            std::cout << "[STABILIZE] Found closer successor: " << inet_ntoa(*(in_addr*)&x.ip) << std::endl;
            successor_list[0] = x;
        }
    }

    void handleNotify(const NodeInfo& potential_pred) {
        if (!predecessor_valid || in_interval(potential_pred.id, predecessor.id, myself.id)) {
            predecessor = potential_pred;
            predecessor_valid = true;
        }
    }

    NodeInfo findSuccessorNextHop(const Sha1ID& target_id) {
        if (in_interval(target_id, myself.id, successor_list[0].id)) {
            return successor_list[0];
        }
        return successor_list[0];
    }

    void handleSetSuccessor(const NodeInfo& new_suc) {
        setSuccessor(new_suc);
    }
    void handleSetPredecessor(const NodeInfo& new_pred) {
        predecessor = new_pred;
        predecessor_valid = true;
    }

    void setCertificate(const uint8_t* data, uint32_t len) {
        if (len > 2048) len = 2048;
        std::memcpy(my_cert, data, len);
        my_cert_len = len;
        has_cert = true;
    }

    bool isAlone() const {
        return successor_list[0].id == myself.id;
    }

    bool needsCertificate() const { return !has_cert; }
    const uint8_t* getCertData() const { return my_cert; }
    uint32_t getCertLen() const { return my_cert_len; }

private:
    NodeInfo myself;
    NodeInfo predecessor;
    bool predecessor_valid;
    NodeInfo successor_list[SUCLIST_SIZE];
    uint8_t my_cert[2048];
    uint32_t my_cert_len = 0;
    bool has_cert = false;
};

#endif