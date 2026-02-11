#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <algorithm>

constexpr uint16_t DEFAULT_PORT = 5000;
constexpr int SUCLIST_SIZE = 3;

struct Sha1ID {
    uint8_t bytes[20];
    bool operator==(const Sha1ID& other) const { return std::memcmp(bytes, other.bytes, 20) == 0; }
    bool operator!=(const Sha1ID& other) const { return !(*this == other); }
    uint8_t toTinyID() const { return bytes[19]; }
};

inline bool in_interval(const Sha1ID& id, const Sha1ID& start, const Sha1ID& end) {
    auto lessThan = [](const Sha1ID& a, const Sha1ID& b) {
        return std::memcmp(a.bytes, b.bytes, 20) < 0;
    };
    if (start == end) return true;
    bool start_lt_end = lessThan(start, end);
    bool start_lt_id = lessThan(start, id);
    bool id_le_end = (lessThan(id, end) || id == end);
    if (start_lt_end) return start_lt_id && id_le_end;
    else return start_lt_id || id_le_end;
}

struct NodeInfo {
    Sha1ID id;
    uint32_t ip;
    uint16_t port;
};

enum MessageType : uint8_t {
    MSG_PING = 0x01,
    MSG_FIND_SUCCESSOR = 0x02,
    MSG_FIND_SUCCESSOR_RESPONSE = 0x03,
    MSG_NOTIFY = 0x04,
    MSG_GET_PREDECESSOR = 0x06,
    MSG_GET_PREDECESSOR_RESPONSE = 0x07,
    MSG_SET_SUCCESSOR = 0x08,
    MSG_SET_PREDECESSOR = 0x09,
    MSG_GET_SUCLIST = 0x0A,
    MSG_GET_SUCLIST_RESPONSE = 0x0B
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t magic;
    uint8_t type;
    uint32_t payload_len;
};

struct FindSuccessorPayload { Sha1ID target_id; };
struct NodeInfoPayload { NodeInfo node; };

struct NodeListPayload {
    uint8_t count;
    NodeInfo nodes[SUCLIST_SIZE];
};
#pragma pack(pop)

#endif