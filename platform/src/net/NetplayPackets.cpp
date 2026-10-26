#include "net/NetplayManager.h"
#include "net/NetplayPackets.h"
#include "port/port.h"

#include <cstring>
#include <algorithm>

namespace {
int64_t getNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void NetplayManager::sendPacket(uint8_t type, const void* payload,
                                size_t payloadSize, const std::string& destIp, int destPort) {
    if (!mSocket.isOpened() || destIp.empty() || destPort <= 0) return;

    size_t totalSize = sizeof(PacketHeader) + payloadSize;
    std::vector<uint8_t> buffer(totalSize);

    PacketHeader* hdr = (PacketHeader*)buffer.data();
    hdr->magic = kNetMagic;
    hdr->version = kNetVersion;
    hdr->type = type;
    hdr->seq = 0;

    if (payload && payloadSize > 0) {
        std::memcpy(buffer.data() + sizeof(PacketHeader), payload, payloadSize);
    }

    mSocket.sendTo(buffer.data(), totalSize, destIp, destPort);
}

void NetplayManager::workerLoop() {
    uint8_t recvBuf[2048];

    while (mRunning.load()) {
        auto now = std::chrono::steady_clock::now();

        if (mRole.load() == PORT_NET_ROLE_HOST) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastBeaconSent).count();
            if (elapsed >= 1000) {
                mLastBeaconSent = now;
                BeaconPayload b;
                std::memset(&b, 0, sizeof(b));
                std::strncpy(b.roomName, mRoomName.c_str(), sizeof(b.roomName) - 1);
                b.mode = (uint8_t)mMode.load();
                std::strncpy(b.stage, "Galaxy", sizeof(b.stage) - 1);
                b.scenario = 1;
                b.playerCount = mConnected.load() ? 2 : 1;
                b.port = (uint16_t)mHostPort;

                uint8_t bBuf[sizeof(PacketHeader) + sizeof(BeaconPayload)];
                PacketHeader* bHdr = (PacketHeader*)bBuf;
                bHdr->magic = kNetMagic;
                bHdr->version = kNetVersion;
                bHdr->type = PKT_BEACON_BROADCAST;
                bHdr->seq = 0;
                std::memcpy(bBuf + sizeof(PacketHeader), &b, sizeof(b));
                mSocket.broadcast(bBuf, sizeof(bBuf), kDefaultPort);
            }
        }

        if (mConnected.load() && !mPeerIp.empty() && mPeerPort > 0) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastPingSent).count() >= 1000) {
                mLastPingSent = now;
                PingPongPayload p;
                p.timestampMs = getNowMs();
                sendPacket(PKT_PING, &p, sizeof(p), mPeerIp, mPeerPort);
            }

            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastPacketReceived).count() >= 5000) {
                PORT_LOG("NetplayManager: peer %s:%d timed out\n", mPeerIp.c_str(), mPeerPort);
                disconnect(false);
            }
        }

        if (mSocket.isOpened()) {
            std::string srcIp;
            int srcPort = 0;
            int bytes = mSocket.receiveFrom(recvBuf, sizeof(recvBuf), srcIp, srcPort);
            if (bytes > 0) {
                handleIncomingPacket(recvBuf, (size_t)bytes, srcIp, srcPort);
            }
        }

        if (mDiscoverySocket.isOpened() && mDiscoverySocket.getPort() != mSocket.getPort()) {
            std::string dIp;
            int dPort = 0;
            int bytes = mDiscoverySocket.receiveFrom(recvBuf, sizeof(recvBuf), dIp, dPort);
            if (bytes > 0) {
                handleIncomingPacket(recvBuf, (size_t)bytes, dIp, dPort);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void NetplayManager::handleIncomingPacket(const uint8_t* data, size_t size,
                                         const std::string& srcIp, int srcPort) {
    if (size < sizeof(PacketHeader)) return;
    const PacketHeader* hdr = (const PacketHeader*)data;
    if (hdr->magic != kNetMagic || hdr->version != kNetVersion) return;

    const uint8_t* payload = data + sizeof(PacketHeader);
    size_t payloadSize = size - sizeof(PacketHeader);

    if (hdr->type == PKT_BEACON_BROADCAST && payloadSize >= sizeof(BeaconPayload)) {
        const BeaconPayload* b = (const BeaconPayload*)payload;
        PortLanHostInfo hostInfo;
        std::memset(&hostInfo, 0, sizeof(hostInfo));
        std::strncpy(hostInfo.name, b->roomName, sizeof(hostInfo.name) - 1);
        std::strncpy(hostInfo.ip, srcIp.c_str(), sizeof(hostInfo.ip) - 1);
        hostInfo.port = b->port;
        hostInfo.mode = b->mode;
        std::strncpy(hostInfo.stage, b->stage, sizeof(hostInfo.stage) - 1);
        hostInfo.scenario = b->scenario;
        hostInfo.playerCount = b->playerCount;
        hostInfo.lastSeenMs = getNowMs();

        std::lock_guard<std::mutex> lk(mMutex);
        bool found = false;
        for (auto& h : mLanHosts) {
            if (std::strcmp(h.ip, hostInfo.ip) == 0 && h.port == hostInfo.port) {
                h = hostInfo;
                found = true;
                break;
            }
        }
        if (!found) {
            mLanHosts.push_back(hostInfo);
            PORT_LOG("NetplayManager: discovered '%s' at %s:%d\n", hostInfo.name, hostInfo.ip, hostInfo.port);
        }
        return;
    }

    if (hdr->type == PKT_CONNECT_REQ && mRole.load() == PORT_NET_ROLE_HOST) {
        mPeerIp = srcIp;
        mPeerPort = srcPort;
        mConnected.store(true);
        mLastPacketReceived = std::chrono::steady_clock::now();

        ConnectAckPayload ack;
        ack.accepted = 1;
        ack.assignedRole = (uint8_t)PORT_NET_ROLE_CLIENT;
        ack.mode = (uint8_t)mMode.load();
        std::strncpy(ack.hostName, mRoomName.c_str(), sizeof(ack.hostName) - 1);
        sendPacket(PKT_CONNECT_ACK, &ack, sizeof(ack), srcIp, srcPort);
        PORT_LOG("NetplayManager: accepted client %s:%d\n", srcIp.c_str(), srcPort);
        return;
    }

    if (hdr->type == PKT_CONNECT_ACK && mRole.load() == PORT_NET_ROLE_CLIENT) {
        const ConnectAckPayload* ack = (const ConnectAckPayload*)payload;
        if (ack->accepted) {
            mConnected.store(true);
            mMode.store(ack->mode);
            mLastPacketReceived = std::chrono::steady_clock::now();
            PORT_LOG("NetplayManager: connected to host %s:%d\n", srcIp.c_str(), srcPort);
        }
        return;
    }

    if (!mConnected.load() || srcIp != mPeerIp || srcPort != mPeerPort) return;
    mLastPacketReceived = std::chrono::steady_clock::now();

    if (hdr->type == PKT_PING && payloadSize >= sizeof(PingPongPayload)) {
        const PingPongPayload* p = (const PingPongPayload*)payload;
        PingPongPayload pong = *p;
        sendPacket(PKT_PONG, &pong, sizeof(pong), srcIp, srcPort);
    } else if (hdr->type == PKT_PONG && payloadSize >= sizeof(PingPongPayload)) {
        const PingPongPayload* p = (const PingPongPayload*)payload;
        int64_t rtt = getNowMs() - p->timestampMs;
        mPingMs.store((int)std::max((int64_t)0, rtt));
    } else if (hdr->type == PKT_COSTAR_STATE && payloadSize >= sizeof(PortPadState)) {
        const PortPadState* pad = (const PortPadState*)payload;
        PortPadState costarPad = *pad;
        costarPad.connected = 1;
        port_input_set(1, &costarPad);
    } else if (hdr->type == PKT_PLAYER_STATE && payloadSize >= sizeof(PortRemotePlayerState)) {
        const PortRemotePlayerState* pState = (const PortRemotePlayerState*)payload;
        std::lock_guard<std::mutex> lk(mMutex);
        mLatestRemotePlayer = *pState;
        mLatestRemotePlayer.valid = 1;
        mHasRemotePlayer = true;
    } else if (hdr->type == PKT_EVENT && payloadSize >= sizeof(EventPayload)) {
        const EventPayload* ev = (const EventPayload*)payload;
        std::lock_guard<std::mutex> lk(mMutex);
        if (ev->eventType == 1) {
            mPendingWarp = true;
            mPendingWarpStage = ev->stage;
            mPendingWarpScenario = ev->scenario;
        } else if (ev->eventType == 2) {
            mPendingSpin = true;
        }
    } else if (hdr->type == PKT_DISCONNECT) {
        PORT_LOG("NetplayManager: disconnect from peer %s:%d\n", srcIp.c_str(), srcPort);
        disconnect(false);
    }
}
