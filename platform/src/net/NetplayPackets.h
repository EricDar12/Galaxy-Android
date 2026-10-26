#pragma once

#include <stdint.h>

#pragma pack(push, 1)

constexpr uint32_t kNetMagic = 0x474D502B; // "GMP+"
constexpr uint8_t  kNetVersion = 1;
constexpr int      kDefaultPort = 24601;

enum PacketType : uint8_t {
    PKT_BEACON_BROADCAST = 1,
    PKT_DISCOVERY_PROBE  = 2,
    PKT_CONNECT_REQ      = 3,
    PKT_CONNECT_ACK      = 4,
    PKT_DISCONNECT       = 5,
    PKT_PING             = 6,
    PKT_PONG             = 7,
    PKT_COSTAR_STATE     = 8,
    PKT_PLAYER_STATE     = 9,
    PKT_EVENT            = 10
};

struct PacketHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint16_t seq;
};

struct BeaconPayload {
    char     roomName[64];
    uint8_t  mode;
    char     stage[64];
    int32_t  scenario;
    uint8_t  playerCount;
    uint16_t port;
};

struct ConnectReqPayload {
    char    playerName[32];
    uint8_t preferredMode;
};

struct ConnectAckPayload {
    uint8_t accepted;
    uint8_t assignedRole;
    uint8_t mode;
    char    hostName[32];
};

struct PingPongPayload {
    int64_t timestampMs;
};

struct EventPayload {
    uint8_t eventType; // 1: Warp, 2: Spin
    char    stage[64];
    int32_t scenario;
};

#pragma pack(pop)
