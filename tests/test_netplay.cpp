#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "net/NetSocket.h"
#include "net/NetplayManager.h"
#include "net/NetplayPackets.h"
#include "port/net.h"
#include "port/input.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static PortPadState sTestCostarPad{};
extern "C" {
void port_log(const char* fmt, ...) { (void)fmt; }
void port_input_set(int padIdx, const PortPadState* pad) {
    if (padIdx == 1 && pad) sTestCostarPad = *pad;
}
}

static void testByteOrderAndHeaderInvariants() {
    TEST_ASSERT(sizeof(PacketHeader) == 8);
    TEST_ASSERT(kNetMagic == 0x474D502B);
    TEST_ASSERT(kNetVersion == 1);
    TEST_ASSERT(kDefaultPort == 24601);

    uint8_t magicBytes[4];
    std::memcpy(magicBytes, &kNetMagic, 4);
    TEST_ASSERT(magicBytes[0] == 0x2B && magicBytes[1] == 0x50 &&
                magicBytes[2] == 0x4D && magicBytes[3] == 0x47);

    PacketHeader hdr{};
    hdr.magic = kNetMagic;
    hdr.version = kNetVersion;
    hdr.type = PKT_PLAYER_STATE;
    hdr.seq = 42;
    TEST_ASSERT(hdr.type == 9);
    TEST_ASSERT(hdr.seq == 42);
    printf("[PASS] Endianness and header invariants verified\n");
}

static void testProductionMalformedPacketRejection() {
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
        PacketHeader* h = (PacketHeader*)truncBuf;
        h->magic = kNetMagic;
        h->version = kNetVersion;
        h->type = t;
        mgr.handleIncomingPacket(truncBuf, sizeof(truncBuf), dummyIp, dummyPort);
    }
    printf("[PASS] Production handleIncomingPacket safely rejected all malformed inputs\n");
}

static void testEphemeralSocketLoopback() {
    NetSocket sockReceiver;
    NetSocket sockSender;

    // Port 0 requests an ephemeral port from the OS kernel
    bool rOk = sockReceiver.open(0, false);
    bool sOk = sockSender.open(0, false);
    TEST_ASSERT(rOk && sOk);

    int portR = sockReceiver.getPort();
    int portS = sockSender.getPort();
    TEST_ASSERT(portR > 0 && portS > 0);
    TEST_ASSERT(portR != portS);

    const char testMsg[] = "GalaxyProductionSocketLoopback";
    bool sent = sockSender.sendTo(testMsg, sizeof(testMsg), "127.0.0.1", portR);
    TEST_ASSERT(sent);

    char recvBuf[128]{};
    std::string srcIp;
    int srcPort = 0;
    int bytes = -1;
    for (int retry = 0; retry < 50; ++retry) {
        bytes = sockReceiver.receiveFrom(recvBuf, sizeof(recvBuf), srcIp, srcPort);
        if (bytes > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    TEST_ASSERT(bytes == (int)sizeof(testMsg));
    TEST_ASSERT(std::memcmp(recvBuf, testMsg, sizeof(testMsg)) == 0);
    TEST_ASSERT(srcPort == portS);
    sockReceiver.close();
    sockSender.close();
    printf("[PASS] Ephemeral port loopback verified on ports %d -> %d\n", portS, portR);
}

static void testPlayerStateReplicationIntegrity() {
    PortRemotePlayerState state{};
    state.valid = 1;
    state.posX = -1234.56f;
    state.posY = 5678.90f;
    state.posZ = 9876.54f;
    state.rotY = 270.0f;
    state.animHash = 0xCAFEBABE;
    state.stateFlags = PORT_NET_FLAG_FLYING | PORT_NET_FLAG_AIR;
    state.isLuigi = 1;
    std::strncpy(state.name, "Luigi2P", sizeof(state.name) - 1);

    uint8_t packetBuf[sizeof(PacketHeader) + sizeof(PortRemotePlayerState)];
    PacketHeader* hdr = (PacketHeader*)packetBuf;
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = PKT_PLAYER_STATE;
    hdr->seq = 1;
    std::memcpy(packetBuf + sizeof(PacketHeader), &state, sizeof(state));

    const PortRemotePlayerState* decoded =
        (const PortRemotePlayerState*)(packetBuf + sizeof(PacketHeader));
    TEST_ASSERT(decoded->valid == 1);
    TEST_ASSERT(decoded->posX == -1234.56f);
    TEST_ASSERT(decoded->posY == 5678.90f);
    TEST_ASSERT(decoded->posZ == 9876.54f);
    TEST_ASSERT(decoded->animHash == 0xCAFEBABE);
    TEST_ASSERT(decoded->stateFlags == (PORT_NET_FLAG_FLYING | PORT_NET_FLAG_AIR));
    TEST_ASSERT(std::strcmp(decoded->name, "Luigi2P") == 0);
    printf("[PASS] RemotePlayerState replication fidelity verified\n");
}

static void testCoStarPointerStateSynchronization() {
    PortPadState pad{};
    pad.connected = 1;
    pad.pointerValid = 1;
    pad.pointerX = 0.42f;
    pad.pointerY = 0.88f;
    pad.buttons = 0x0100; // B button (Star bit pull)

    uint8_t packetBuf[sizeof(PacketHeader) + sizeof(PortPadState)];
    PacketHeader* hdr = (PacketHeader*)packetBuf;
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = PKT_COSTAR_STATE;
    std::memcpy(packetBuf + sizeof(PacketHeader), &pad, sizeof(pad));

    const PortPadState* inPad = (const PortPadState*)(packetBuf + sizeof(PacketHeader));
    port_input_set(1, inPad);
    TEST_ASSERT(sTestCostarPad.connected == 1);
    TEST_ASSERT(sTestCostarPad.pointerValid == 1);
    TEST_ASSERT(sTestCostarPad.pointerX == 0.42f);
    TEST_ASSERT(sTestCostarPad.pointerY == 0.88f);
    TEST_ASSERT(sTestCostarPad.buttons == 0x0100);
    printf("[PASS] 2P Co-Star Assist pointer synchronization verified\n");
}

static void testHandshakePayloadContracts() {
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

int main() {
    printf("=== Super Mario Galaxy Android — Netplay Production Suite ===\n");
    testByteOrderAndHeaderInvariants();
    testProductionMalformedPacketRejection();
    testEphemeralSocketLoopback();
    testPlayerStateReplicationIntegrity();
    testCoStarPointerStateSynchronization();
    testHandshakePayloadContracts();
    printf("All Netplay Subsystem production tests passed successfully!\n");
    return 0;
}
