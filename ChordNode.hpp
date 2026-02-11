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
        // Simuliere ID-Generierung (in echt: SHA1 library)
        std::memset(myself.id.bytes, 0, 20);
        myself.id.bytes[19] = (uint8_t)(my_port & 0xFF); // ID basiert auf Port für Demo

        successor = myself;
        predecessor_valid = false;

        std::cout << "[NODE] Started on Port " << my_port << " with ID ending in " << (int)myself.id.bytes[19] << std::endl;
    }

    NodeInfo getSuccessor() const { return successor; }
    NodeInfo getMyself() const { return myself; }

    // --- LOGIK: JOIN ---
    // Wird aufgerufen, wenn wir Antwort vom Bootstrap-Node bekommen
    void handleJoinResponse(const NodeInfo& new_successor) {
        successor = new_successor;
        std::cout << "[NODE] Joined! New Successor is Port " << successor.port << std::endl;
    }

    // --- LOGIK: FIND SUCCESSOR ---
    // Entscheidet: Bin ich zuständig oder wer anders?
    // Return: Die NodeInfo, an die wir den Request weiterleiten müssen (oder das Ergebnis)
    NodeInfo findSuccessorLogic(const Sha1ID& target_id) {
        // Vereinfachtes Routing (Linear) für den Anfang:
        // Wenn target zwischen mir und Successor liegt -> Successor ist zuständig.
        // Sonst -> Anfrage an Successor weiterleiten.

        (void)target_id;
        // TODO: Hier echte Intervall-Prüfung und Finger-Table einbauen.
        // Für PoC leiten wir einfach IMMER an den Successor weiter, es sei denn, wir sind es selbst.

        if (successor.id == myself.id) {
            return myself;
        }
        return successor;
    }

    // --- LOGIK: STABILIZE ---
    // Wird periodisch vom Main-Loop aufgerufen
    void stabilize() {
        // Hier würde man normalerweise "get_predecessor" an den Successor senden.
        // Das triggert im Main-Loop ein Netzwerk-Paket.
        // std::cout << "[NODE] Stabilizing..." << std::endl;
    }

    void handleNotify(const NodeInfo& potential_predecessor) {
        // Prüfen ob der neue Predecessor näher ist (hier vereinfacht immer ja)
        predecessor = potential_predecessor;
        predecessor_valid = true;
        std::cout << "[NODE] New Predecessor: Port " << predecessor.port << std::endl;
    }

private:
    NodeInfo myself;
    NodeInfo successor;
    NodeInfo predecessor;
    bool predecessor_valid;

    // Finger Table (statisch reserviert)
    std::array<NodeInfo, 160> fingers;
};

#endif