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

        // ID Generierung: Wir nutzen hier der Einfachheit halber den Port als Hash
        // (In Produktion: mbedTLS SHA1 nutzen)
        std::memset(myself.id.bytes, 0, 20);
        myself.id.bytes[19] = (uint8_t)(my_port & 0xFF);

        successor = myself;
        std::memset(predecessor.id.bytes, 0, 20); // Leer initialisieren
        has_predecessor = false;

        std::cout << "[NODE] Init ID: " << (int)myself.id.toTinyID() << " (Port " << my_port << ")" << std::endl;
    }

    NodeInfo getSuccessor() const { return successor; }
    NodeInfo getPredecessor() const { return predecessor; }
    bool hasPredecessor() const { return has_predecessor; }
    NodeInfo getMyself() const { return myself; }

    // Setzt den Successor hart (z.B. nach Join Response)
    void setSuccessor(const NodeInfo& new_suc) {
        successor = new_suc;
        std::cout << "[NODE] Successor updated -> " << (int)successor.id.toTinyID() << " (Port " << successor.port << ")" << std::endl;
    }

    // Gibt entweder (true, ziel_node) zurück, wenn wir die Antwort wissen,
    // oder (false, naechster_hop), wenn wir weiterfragen müssen.
    NodeInfo findSuccessorNextHop(const Sha1ID& target_id) {
        // Fall 1: ID liegt zwischen mir und Successor -> Successor ist zuständig
        if (in_interval(target_id, myself.id, successor.id)) {
            return successor;
        }
        // Fall 2: Sonst leiten wir an Successor weiter (hier keine Finger Table Optimierung für PoC)
        return successor;
    }

    // Prüft, ob der Predecessor meines Successors besser zu mir passt
    void handleStabilizeResponse(const NodeInfo& x) {
        // x ist der Predecessor meines Successors.
        // Wenn x zwischen mir und meinem Successor liegt, ist x mein neuer Successor.
        if (in_interval(x.id, myself.id, successor.id)) {
            std::cout << "[STABILIZE] Found better successor: " << (int)x.id.toTinyID() << std::endl;
            successor = x;
        }
    }

    // Jemand behauptet, mein Predecessor zu sein
    void handleNotify(const NodeInfo& potential_pred) {
        if (!has_predecessor || in_interval(potential_pred.id, predecessor.id, myself.id)) {
            predecessor = potential_pred;
            has_predecessor = true;
            std::cout << "[INFO] New Predecessor accepted: " << (int)predecessor.id.toTinyID() << " (Port " << predecessor.port << ")" << std::endl;
        }
    }

    // Wird aufgerufen, wenn wir MSG_SET_SUCCESSOR empfangen
    void handleSetSuccessor(const NodeInfo& new_suc) {
        successor = new_suc;
        std::cout << "[LEAVE-OP] My Successor was updated to Port " << successor.port << std::endl;
    }

    // Wird aufgerufen, wenn wir MSG_SET_PREDECESSOR empfangen
    void handleSetPredecessor(const NodeInfo& new_pred) {
        predecessor = new_pred;
        has_predecessor = true;
        std::cout << "[LEAVE-OP] My Predecessor was updated to Port " << predecessor.port << std::endl;
    }

private:
    NodeInfo myself;
    NodeInfo successor;
    NodeInfo predecessor;
    bool has_predecessor;
};

#endif