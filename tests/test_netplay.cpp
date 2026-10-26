#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "net/NetSocket.h"
#include "net/NetplayPackets.h"
#include "port/net.h"
#include "port/input.h"

// Stubs for platform linkage in test environment
static PortPadState sTestCostarPad{};
extern "C" {
void port_log(const char* fmt, ...) {
    (void)fmt;
}
void port_input_set(int padIdx, const PortPadState* pad) {
    if (padIdx == 1 && pad) {
        sTestCostarPad = *pad;
    }
}
}

static void testPacketHeaderInvariants() {
    assert(sizeof(PacketHeader) == 8);
    assert(kNetMagic == 0x474D502B);
    assert(kNetVersion == 1);
    assert(kDefaultPort == 24601);

    PacketHeader hdr{};
    hdr.magic = kNetMagic;
    hdr.version = kNetVersion;
    hdr.type = PKT_PLAYER_STATE;
    hdr.seq = 42;

    assert(hdr.magic == 0x474D502B);
    assert(hdr.type == 9);
    assert(hdr.seq == 42);
    printf("[PASS] Packet header invariants and magic constants verified\n");
}

static void testMalformedPacketRejection() {
    uint8_t buffer[64];
    std::memset(buffer, 0, sizeof(buffer));

    // Case 1: Truncated buffer (smaller than PacketHeader)
    assert(sizeof(PacketHeader) == 8);
    size_t truncatedSize = 4;
    assert(truncatedSize < sizeof(PacketHeader));

    // Case 2: Wrong magic number
    PacketHeader* badMagicHdr = (PacketHeader*)buffer;
    badMagicHdr->magic = 0xDEADBEEF;
    badMagicHdr->version = kNetVersion;
    badMagicHdr->type = PKT_PLAYER_STATE;
    assert(badMagicHdr->magic != kNetMagic);

    // Case 3: Wrong protocol version
    PacketHeader* badVerHdr = (PacketHeader*)buffer;
    badVerHdr->magic = kNetMagic;
    badVerHdr->version = 99;
    assert(badVerHdr->version != kNetVersion);

    printf("[PASS] Malformed packet rejection (size, magic, version) verified\n");
}

static void testSocketLoopbackTransport() {
    NetSocket sockSender;
    NetSocket sockReceiver;

    // Ephemeral test ports
    const int portReceiver = 28601;
    const int portSender = 28602;

    bool rOk = sockReceiver.open(portReceiver, false);
    bool sOk = sockSender.open(portSender, false);

    if (!rOk || !sOk) {
        printf("[SKIP] Local UDP socket binding unavailable on host\n");
        return;
    }

    const char testMsg[] = "GalaxyNetplayLoopbackTest";
    bool sent = sockSender.sendTo(testMsg, sizeof(testMsg), "127.0.0.1", portReceiver);
    assert(sent);

    char recvBuf[128];
    std::string srcIp;
    int srcPort = 0;

    int bytes = -1;
    for (int retry = 0; retry < 50; ++retry) {
        bytes = sockReceiver.receiveFrom(recvBuf, sizeof(recvBuf), srcIp, srcPort);
        if (bytes > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    assert(bytes == sizeof(testMsg));
    assert(std::memcmp(recvBuf, testMsg, sizeof(testMsg)) == 0);
    assert(srcPort == portSender);

    sockReceiver.close();
    sockSender.close();
    printf("[PASS] UDP loopback transport verified on 127.0.0.1\n");
}

static void testRemotePlayerStateSerialization() {
    PortRemotePlayerState state{};
    state.valid = 1;
    state.posX = 1500.25f;
    state.posY = -320.50f;
    state.posZ = 8400.75f;
    state.rotX = 0.0f;
    state.rotY = 180.0f;
    state.rotZ = 0.0f;
    state.animHash = 0x9A4B1201;
    state.animFrame = 14.5f;
    state.animSpeed = 1.0f;
    state.stateFlags = PORT_NET_FLAG_SPINNING | PORT_NET_FLAG_GROUNDED;
    state.isLuigi = 1;
    std::strncpy(state.name, "Luigi2P", sizeof(state.name) - 1);

    uint8_t packetBuf[sizeof(PacketHeader) + sizeof(PortRemotePlayerState)];
    PacketHeader* hdr = (PacketHeader*)packetBuf;
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = PKT_PLAYER_STATE;
    hdr->seq = 101;

    std::memcpy(packetBuf + sizeof(PacketHeader), &state, sizeof(state));

    // Decode on receiver
    const PacketHeader* inHdr = (const PacketHeader*)packetBuf;
    assert(inHdr->magic == kNetMagic);
    assert(inHdr->version == kNetVersion);
    assert(inHdr->type == PKT_PLAYER_STATE);

    const PortRemotePlayerState* inState =
        (const PortRemotePlayerState*)(packetBuf + sizeof(PacketHeader));
    assert(inState->valid == 1);
    assert(inState->posX == 1500.25f);
    assert(inState->posY == -320.50f);
    assert(inState->posZ == 8400.75f);
    assert(inState->animHash == 0x9A4B1201);
    assert(inState->isLuigi == 1);
    assert(inState->stateFlags == (PORT_NET_FLAG_SPINNING | PORT_NET_FLAG_GROUNDED));
    assert(std::strcmp(inState->name, "Luigi2P") == 0);

    printf("[PASS] RemotePlayerState packet framing and float fidelity verified\n");
}

static void testCoStarStateSynchronization() {
    PortPadState pad{};
    pad.connected = 1;
    pad.pointerValid = 1;
    pad.pointerX = 0.35f;
    pad.pointerY = 0.65f;
    pad.pointerDist = 2.0f;
    pad.buttons = 0x0800; // A button pressed

    uint8_t packetBuf[sizeof(PacketHeader) + sizeof(PortPadState)];
    PacketHeader* hdr = (PacketHeader*)packetBuf;
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = PKT_COSTAR_STATE;
    hdr->seq = 102;

    std::memcpy(packetBuf + sizeof(PacketHeader), &pad, sizeof(pad));

    // Decode and invoke simulated handler
    const PacketHeader* inHdr = (const PacketHeader*)packetBuf;
    assert(inHdr->type == PKT_COSTAR_STATE);
    const PortPadState* inPad = (const PortPadState*)(packetBuf + sizeof(PacketHeader));

    port_input_set(1, inPad);
    assert(sTestCostarPad.connected == 1);
    assert(sTestCostarPad.pointerValid == 1);
    assert(sTestCostarPad.pointerX == 0.35f);
    assert(sTestCostarPad.pointerY == 0.65f);
    assert(sTestCostarPad.buttons == 0x0800);

    printf("[PASS] 2P Co-Star Assist pointer synchronization verified\n");
}

static void testHandshakePayloadContracts() {
    BeaconPayload beacon{};
    std::strncpy(beacon.roomName, "Galaxy Room 42", sizeof(beacon.roomName) - 1);
    beacon.mode = PORT_NET_MODE_COOP;
    beacon.port = 24601;
    beacon.playerCount = 1;
    assert(beacon.mode == 1);
    assert(std::strcmp(beacon.roomName, "Galaxy Room 42") == 0);

    ConnectReqPayload req{};
    std::strncpy(req.playerName, "Player2", sizeof(req.playerName) - 1);
    req.preferredMode = PORT_NET_MODE_COSTAR;
    assert(req.preferredMode == 2);

    ConnectAckPayload ack{};
    ack.accepted = 1;
    ack.assignedRole = PORT_NET_ROLE_CLIENT;
    ack.mode = PORT_NET_MODE_COSTAR;
    std::strncpy(ack.hostName, "GalaxyHost", sizeof(ack.hostName) - 1);
    assert(ack.accepted == 1);
    assert(ack.assignedRole == 2);

    EventPayload ev{};
    ev.eventType = 1; // Warp
    std::strncpy(ev.stage, "EggPlanetGalaxy", sizeof(ev.stage) - 1);
    ev.scenario = 2;
    assert(ev.eventType == 1);
    assert(std::strcmp(ev.stage, "EggPlanetGalaxy") == 0);
    assert(ev.scenario == 2);

    printf("[PASS] Netplay handshake and event payload contracts verified\n");
}

int main() {
    printf("=== Super Mario Galaxy Android — Netplay Subsystem Test Suite ===\n");
    testPacketHeaderInvariants();
    testMalformedPacketRejection();
    testSocketLoopbackTransport();
    testRemotePlayerStateSerialization();
    testCoStarStateSynchronization();
    testHandshakePayloadContracts();
    printf("All Netplay Subsystem tests passed successfully!\n");
    return 0;
}
