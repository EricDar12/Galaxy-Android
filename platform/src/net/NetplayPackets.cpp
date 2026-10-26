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

    netWriteLe32(buffer.data(), kNetMagic);
    buffer[4] = kNetVersion;
    buffer[5] = type;
    netWriteLe16(buffer.data() + 6, 0);

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
                netWriteLe32(bBuf, kNetMagic);
                bBuf[4] = kNetVersion;
                bBuf[5] = PKT_BEACON_BROADCAST;
                netWriteLe16(bBuf + 6, 0);
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
    if (!data || size < sizeof(PacketHeader)) return;
    uint32_t magic = netReadLe32(data);
    uint8_t version = data[4];
    uint8_t type = data[5];
    if (magic != kNetMagic || version != kNetVersion) return;
    if (type < PKT_BEACON_BROADCAST || type > PKT_EVENT) return;

    const uint8_t* payload = data + sizeof(PacketHeader);
    size_t payloadSize = size - sizeof(PacketHeader);

    if (type == PKT_BEACON_BROADCAST) {
        if (payloadSize < sizeof(BeaconPayload)) return;
        BeaconPayload b;
        std::memcpy(&b, payload, sizeof(BeaconPayload));
        uint16_t port = netReadLe16((const uint8_t*)&b.port);
        if (port == 0) return;
        if (b.mode < PORT_NET_MODE_COOP || b.mode > PORT_NET_MODE_COSTAR) return;

        PortLanHostInfo hostInfo;
        std::memset(&hostInfo, 0, sizeof(hostInfo));
        std::strncpy(hostInfo.name, b.roomName, sizeof(hostInfo.name) - 1);
        hostInfo.name[sizeof(hostInfo.name) - 1] = '\0';
        std::strncpy(hostInfo.ip, srcIp.c_str(), sizeof(hostInfo.ip) - 1);
        hostInfo.ip[sizeof(hostInfo.ip) - 1] = '\0';
        hostInfo.port = port;
        hostInfo.mode = b.mode;
        std::strncpy(hostInfo.stage, b.stage, sizeof(hostInfo.stage) - 1);
        hostInfo.stage[sizeof(hostInfo.stage) - 1] = '\0';
        hostInfo.scenario = netReadLeI32((const uint8_t*)&b.scenario);
        hostInfo.playerCount = b.playerCount;
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

    if (type == PKT_CONNECT_REQ) {
        if (mRole.load() != PORT_NET_ROLE_HOST || mConnected.load()) return;
        if (payloadSize < sizeof(ConnectReqPayload)) return;
        ConnectReqPayload req;
        std::memcpy(&req, payload, sizeof(ConnectReqPayload));
        if (req.preferredMode < PORT_NET_MODE_COOP || req.preferredMode > PORT_NET_MODE_COSTAR) return;
        if (req.playerName[0] == '\0') return;

        mPeerIp = srcIp;
        mPeerPort = srcPort;
        mConnected.store(true);
        mLastPacketReceived = std::chrono::steady_clock::now();

        ConnectAckPayload ack{};
        ack.accepted = 1;
        ack.assignedRole = (uint8_t)PORT_NET_ROLE_CLIENT;
        ack.mode = (uint8_t)mMode.load();
        std::strncpy(ack.hostName, mRoomName.c_str(), sizeof(ack.hostName) - 1);
        ack.hostName[sizeof(ack.hostName) - 1] = '\0';
        sendPacket(PKT_CONNECT_ACK, &ack, sizeof(ack), srcIp, srcPort);
        PORT_LOG("NetplayManager: accepted client %s:%d\n", srcIp.c_str(), srcPort);
        return;
    }

    if (type == PKT_CONNECT_ACK) {
        if (mRole.load() != PORT_NET_ROLE_CLIENT || mConnected.load()) return;
        if (payloadSize < sizeof(ConnectAckPayload)) return;
        ConnectAckPayload ack;
        std::memcpy(&ack, payload, sizeof(ConnectAckPayload));
        if (ack.accepted && ack.assignedRole == (uint8_t)PORT_NET_ROLE_CLIENT &&
            ack.mode >= PORT_NET_MODE_COOP && ack.mode <= PORT_NET_MODE_COSTAR) {
            mConnected.store(true);
            mMode.store(ack.mode);
            mLastPacketReceived = std::chrono::steady_clock::now();
            PORT_LOG("NetplayManager: connected to host %s:%d\n", srcIp.c_str(), srcPort);
        }
        return;
    }

    if (!mConnected.load() || srcIp != mPeerIp || srcPort != mPeerPort) return;

    if (type == PKT_PING && payloadSize >= sizeof(PingPongPayload)) {
        mLastPacketReceived = std::chrono::steady_clock::now();
        PingPongPayload p;
        std::memcpy(&p, payload, sizeof(PingPongPayload));
        sendPacket(PKT_PONG, &p, sizeof(p), srcIp, srcPort);
    } else if (type == PKT_PONG && payloadSize >= sizeof(PingPongPayload)) {
        mLastPacketReceived = std::chrono::steady_clock::now();
        PingPongPayload p;
        std::memcpy(&p, payload, sizeof(PingPongPayload));
        int64_t rtt = getNowMs() - netReadLeI64((const uint8_t*)&p.timestampMs);
        mPingMs.store((int)std::max((int64_t)0, rtt));
    } else if (type == PKT_COSTAR_STATE && payloadSize >= sizeof(PortPadState)) {
        mLastPacketReceived = std::chrono::steady_clock::now();
        PortPadState pad;
        std::memcpy(&pad, payload, sizeof(PortPadState));
        pad.connected = 1;
        port_input_set(1, &pad);
    } else if (type == PKT_PLAYER_STATE && payloadSize >= sizeof(PortRemotePlayerState)) {
        PortRemotePlayerState pState;
        std::memcpy(&pState, payload, sizeof(PortRemotePlayerState));
        if (pState.valid == 1) {
            mLastPacketReceived = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lk(mMutex);
            mLatestRemotePlayer = pState;
            mHasRemotePlayer = true;
        }
    } else if (type == PKT_EVENT && payloadSize >= sizeof(EventPayload)) {
        EventPayload ev;
        std::memcpy(&ev, payload, sizeof(EventPayload));
        if (ev.eventType == 1 || ev.eventType == 2) {
            mLastPacketReceived = std::chrono::steady_clock::now();
            char safeStage[sizeof(ev.stage) + 1] = {0};
            std::memcpy(safeStage, ev.stage, sizeof(ev.stage));
            std::lock_guard<std::mutex> lk(mMutex);
            if (ev.eventType == 1) {
                mPendingWarp = true;
                mPendingWarpStage = safeStage;
                mPendingWarpScenario = netReadLeI32((const uint8_t*)&ev.scenario);
            } else if (ev.eventType == 2) {
                mPendingSpin = true;
            }
        }
    } else if (type == PKT_DISCONNECT) {
        mLastPacketReceived = std::chrono::steady_clock::now();
        PORT_LOG("NetplayManager: disconnect from peer %s:%d\n", srcIp.c_str(), srcPort);
        disconnect(false);
    }
}
