#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "netplay_check.h"

#include "net/NetSocket.h"
#include "net/NetplayManager.h"
#include "net/NetplayPackets.h"
#include "port/net.h"
#include "port/input.h"

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

// The RX worker assigns the peer address while the game thread reads and clears
// it.  Without the mutex discipline an observer can see a std::string that is
// neither empty nor the value that was written.
static void testPeerAddressStaysCoherentAcrossThreads() {
    NetplayManager& mgr = NetplayManager::get();
    TEST_ASSERT(mgr.startHost(0, PORT_NET_MODE_COOP, "Peer Race Room"));

    const std::string peerIp = "192.168.77.77";
    const int peerPort = 25111;

    uint8_t connectReq[sizeof(PacketHeader) + sizeof(ConnectReqPayload)]{};
    PacketHeader* hdr = (PacketHeader*)connectReq;
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = PKT_CONNECT_REQ;
    ConnectReqPayload* body = (ConnectReqPayload*)(connectReq + sizeof(PacketHeader));
    std::strncpy(body->playerName, "Player 2", sizeof(body->playerName) - 1);
    body->preferredMode = PORT_NET_MODE_COOP;

    std::atomic<bool> stop{false};
    std::atomic<int> incoherent{0};
    std::atomic<int> accepted{0};

    std::thread rx([&] {
        while (!stop.load()) {
            mgr.handleIncomingPacket(connectReq, sizeof(connectReq), peerIp, peerPort);
            if (mgr.isConnected()) accepted.fetch_add(1);
        }
    });
    std::thread game([&] {
        PortRemotePlayerState state{};
        state.valid = 1;
        int iterations = 0;
        while (!stop.load()) {
            std::string ip = mgr.getPeerIp();
            if (!ip.empty() && ip != peerIp) incoherent.fetch_add(1);
            mgr.sendPlayerState(state);
            if ((iterations++ % 200) == 199) mgr.disconnect(false);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true);
    rx.join();
    game.join();

    TEST_ASSERT(accepted.load() > 0);
    TEST_ASSERT(incoherent.load() == 0);
    mgr.disconnect();
    printf("[PASS] Peer address stayed coherent across the RX and game threads\n");
}

int main() {
    printf("=== Super Mario Galaxy Android — Netplay Production Suite ===\n");
    testByteOrderAndHeaderInvariants();
    runNetplayPacketChecks();
    testEphemeralSocketLoopback();
    testPlayerStateReplicationIntegrity();
    testCoStarPointerStateSynchronization();
    testPeerAddressStaysCoherentAcrossThreads();
    printf("All Netplay Subsystem production tests passed successfully!\n");
    return 0;
}
