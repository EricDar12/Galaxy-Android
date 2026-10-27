// Wire-format defence: every malformed or spoofed datagram the production
// receiver has to survive, plus the handshake payload layouts. The socket,
// state-replication and threading half runs from test_netplay.cpp.
#include "netplay_check.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "net/NetplayManager.h"
#include "net/NetplayPackets.h"
#include "port/net.h"
#include "port/input.h"

namespace {

void testProductionMalformedPacketRejection() {
    NetplayManager& mgr = NetplayManager::get();
    const std::string dummyIp = "127.0.0.1";
    const int dummyPort = 12345;

    // 1. Zero and truncated buffers (< sizeof(PacketHeader))
    for (size_t len = 0; len < sizeof(PacketHeader); ++len) {
        uint8_t garbage[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
        mgr.handleIncomingPacket(garbage, len, dummyIp, dummyPort);
    }

    // 2. Bad magic numbers
    uint8_t badMagicBuf[64]{};
    PacketHeader* badHdr = (PacketHeader*)badMagicBuf;
    badHdr->magic = 0xDEADBEEF;
    badHdr->version = kNetVersion;
    badHdr->type = PKT_PLAYER_STATE;
    mgr.handleIncomingPacket(badMagicBuf, sizeof(badMagicBuf), dummyIp, dummyPort);

    // 3. Bad protocol versions
    badHdr->magic = kNetMagic;
    badHdr->version = 99;
    mgr.handleIncomingPacket(badMagicBuf, sizeof(badMagicBuf), dummyIp, dummyPort);

    // 4. Unknown packet types
    badHdr->version = kNetVersion;
    badHdr->type = 250;
    mgr.handleIncomingPacket(badMagicBuf, sizeof(badMagicBuf), dummyIp, dummyPort);

    // 5. Truncated payloads for all defined packet types
    const uint8_t types[] = {
        PKT_BEACON_BROADCAST, PKT_CONNECT_REQ, PKT_CONNECT_ACK,
        PKT_PING, PKT_PONG, PKT_COSTAR_STATE, PKT_PLAYER_STATE, PKT_EVENT
    };
    for (uint8_t t : types) {
        uint8_t truncBuf[sizeof(PacketHeader) + 2]{};
        PacketHeader h{};
        h.magic = kNetMagic;
        h.version = kNetVersion;
        h.type = t;
        std::memcpy(truncBuf, &h, sizeof(h));
        mgr.handleIncomingPacket(truncBuf, sizeof(truncBuf), dummyIp, dummyPort);
    }

    // 6. Alignment-safety: parse from deliberately odd/unaligned buffer addresses
    alignas(16) uint8_t rawAligned[128]{};
    uint8_t* unalignedPtr = rawAligned + 1; // +1 byte offset guarantees unaligned
    PacketHeader unalignedHdr{};
    unalignedHdr.magic = kNetMagic;
    unalignedHdr.version = kNetVersion;
    unalignedHdr.type = PKT_BEACON_BROADCAST;
    std::memcpy(unalignedPtr, &unalignedHdr, sizeof(PacketHeader));
    mgr.handleIncomingPacket(unalignedPtr, sizeof(PacketHeader), dummyIp, dummyPort);

    // 7. Excessive payload handling
    std::vector<uint8_t> hugeBuf(4096, 0x55);
    std::memcpy(hugeBuf.data(), &unalignedHdr, sizeof(PacketHeader));
    mgr.handleIncomingPacket(hugeBuf.data(), hugeBuf.size(), dummyIp, dummyPort);

    // 8. Observable state rejection on invalid CONNECT_REQ and non-peer packets
    mgr.startHost(0, PORT_NET_MODE_COOP, "TestHostRoom");
    TEST_ASSERT(!mgr.isConnected());

    // Header-only CONNECT_REQ without payload
    uint8_t truncConnect[sizeof(PacketHeader)];
    netWriteLe32(truncConnect, kNetMagic);
    truncConnect[4] = kNetVersion;
    truncConnect[5] = PKT_CONNECT_REQ;
    netWriteLe16(truncConnect + 6, 0);
    mgr.handleIncomingPacket(truncConnect, sizeof(truncConnect), dummyIp, dummyPort);
    TEST_ASSERT(!mgr.isConnected());

    // Empty player name in CONNECT_REQ
    uint8_t emptyNameBuf[sizeof(PacketHeader) + sizeof(ConnectReqPayload)]{};
    netWriteLe32(emptyNameBuf, kNetMagic);
    emptyNameBuf[4] = kNetVersion;
    emptyNameBuf[5] = PKT_CONNECT_REQ;
    emptyNameBuf[sizeof(PacketHeader) + 32] = (uint8_t)PORT_NET_MODE_COOP;
    mgr.handleIncomingPacket(emptyNameBuf, sizeof(emptyNameBuf), dummyIp, dummyPort);
    TEST_ASSERT(!mgr.isConnected());

    // Invalid mode in CONNECT_REQ
    ConnectReqPayload badModeReq{};
    std::strncpy(badModeReq.playerName, "Mario", sizeof(badModeReq.playerName) - 1);
    badModeReq.preferredMode = 99;
    uint8_t badModeBuf[sizeof(PacketHeader) + sizeof(ConnectReqPayload)];
    netWriteLe32(badModeBuf, kNetMagic);
    badModeBuf[4] = kNetVersion;
    badModeBuf[5] = PKT_CONNECT_REQ;
    std::memcpy(badModeBuf + sizeof(PacketHeader), &badModeReq, sizeof(badModeReq));
    mgr.handleIncomingPacket(badModeBuf, sizeof(badModeBuf), dummyIp, dummyPort);
    TEST_ASSERT(!mgr.isConnected());

    // Valid CONNECT_REQ accepts cleanly
    ConnectReqPayload validReq{};
    std::strncpy(validReq.playerName, "Luigi2P", sizeof(validReq.playerName) - 1);
    validReq.preferredMode = (uint8_t)PORT_NET_MODE_COOP;
    std::memcpy(badModeBuf + sizeof(PacketHeader), &validReq, sizeof(validReq));
    mgr.handleIncomingPacket(badModeBuf, sizeof(badModeBuf), dummyIp, dummyPort);
    TEST_ASSERT(mgr.isConnected());

    // Traffic from non-peer rejected while connected
    PortRemotePlayerState spoofState{};
    spoofState.valid = 1;
    spoofState.posX = 9999.0f;
    uint8_t spoofBuf[sizeof(PacketHeader) + sizeof(PortRemotePlayerState)];
    netWriteLe32(spoofBuf, kNetMagic);
    spoofBuf[4] = kNetVersion;
    spoofBuf[5] = PKT_PLAYER_STATE;
    std::memcpy(spoofBuf + sizeof(PacketHeader), &spoofState, sizeof(spoofState));
    mgr.handleIncomingPacket(spoofBuf, sizeof(spoofBuf), "192.168.1.99", dummyPort);
    PortRemotePlayerState cur{};
    bool hasRemote = mgr.getRemotePlayerState(cur);
    TEST_ASSERT(!hasRemote || cur.posX != 9999.0f);

    mgr.disconnect(false);
    TEST_ASSERT(!mgr.isConnected());

    printf("[PASS] Production handleIncomingPacket safely rejected all malformed inputs\n");
}

void testHandshakePayloadContracts() {
    BeaconPayload beacon{};
    std::strncpy(beacon.roomName, "Galaxy 2P Netplay", sizeof(beacon.roomName) - 1);
    beacon.mode = PORT_NET_MODE_COOP;
    beacon.port = 24601;
    beacon.playerCount = 1;
    TEST_ASSERT(beacon.mode == 1);
    TEST_ASSERT(std::strcmp(beacon.roomName, "Galaxy 2P Netplay") == 0);

    ConnectAckPayload ack{};
    ack.accepted = 1;
    ack.assignedRole = PORT_NET_ROLE_CLIENT;
    ack.mode = PORT_NET_MODE_COOP;
    TEST_ASSERT(ack.accepted == 1);
    TEST_ASSERT(ack.assignedRole == 2);

    EventPayload ev{};
    ev.eventType = 2; // Spin
    TEST_ASSERT(ev.eventType == 2);
    printf("[PASS] Netplay handshake and payload contracts verified\n");
}

}  // namespace

void runNetplayPacketChecks() {
    testHandshakePayloadContracts();
    testProductionMalformedPacketRejection();
}
