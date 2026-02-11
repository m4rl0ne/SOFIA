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
        myself.id.bytes[19] = (uint8_t)(my_port & 0xFF);

        for(int i=0; i<SUCLIST_SIZE; ++i) successor_list[i] = myself;

        predecessor_valid = false;
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
        std::cout << "[UPDATE] Successor set to " << new_suc.port << " (List Reset)" << std::endl;
    }

    void invalidatePredecessor() {
        predecessor_valid = false;
        // Optional: ID auf 0 setzen zur Sicherheit
        std::memset(predecessor.id.bytes, 0, 20);
        predecessor.port = 0;
    }

    void handleSuccessorFailure() {
        std::cout << "[FAILOVER] Successor " << successor_list[0].port << " unreachable!" << std::endl;

        for(int i=0; i < SUCLIST_SIZE-1; ++i) {
            successor_list[i] = successor_list[i+1];
        }
        successor_list[SUCLIST_SIZE-1] = myself;

        std::cout << "[FAILOVER] New Successor is " << successor_list[0].port << std::endl;

        invalidatePredecessor();
    }

    void updateSuccessorList(const NodeInfo* received_list, int count) {
        bool changed = false;

        for(int i=0; i < count && i < (SUCLIST_SIZE-1); ++i) {
            if (successor_list[i+1].port != received_list[i].port) {
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

            if (x.port == successor_list[0].port) return;

            std::cout << "[STABILIZE] Found closer successor: " << x.port << std::endl;
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

private:
    NodeInfo myself;
    NodeInfo predecessor;
    bool predecessor_valid;
    std::array<NodeInfo, SUCLIST_SIZE> successor_list;
};

#endif