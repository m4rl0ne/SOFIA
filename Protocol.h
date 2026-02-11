#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

// --- KONFIGURATION ---
constexpr uint16_t DEFAULT_PORT = 5000;

// --- ID STRUKTUR (20 Byte SHA-1) ---
struct Sha1ID {
    uint8_t bytes[20];

    bool operator==(const Sha1ID& other) const {
        return std::memcmp(bytes, other.bytes, 20) == 0;
    }
    // Für Routing wichtig:
    bool operator!=(const Sha1ID& other) const { return !(*this == other); }
};

// --- ADRESS INFO ---
// IP als 32-Bit Integer (Network Byte Order)
struct NodeInfo {
    Sha1ID id;
    uint32_t ip;
    uint16_t port;
};

// --- NACHRICHTEN TYPEN ---
enum MessageType : uint8_t {
    MSG_PING = 0x01,
    MSG_FIND_SUCCESSOR = 0x02,
    MSG_FIND_SUCCESSOR_RESPONSE = 0x03,
    MSG_NOTIFY = 0x04,
    MSG_JOIN_REQ = 0x05
};

// --- PACKET HEADER ---
// Jedes Paket beginnt damit.
#pragma pack(push, 1) // Packing erzwingen (kein Padding)
struct PacketHeader {
    uint8_t magic;      // 0xCH (Chord) zur Erkennung
    uint8_t type;       // MessageType
    uint32_t payload_len;
};

// Payload für FindSuccessor
struct FindSuccessorPayload {
    Sha1ID target_id;
};

// Payload für Antworten, die eine NodeInfo enthalten
struct NodeInfoPayload {
    NodeInfo node;
};
#pragma pack(pop)

#endif