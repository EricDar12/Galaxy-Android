#include "net/NetplayManager.h"
#include "net/NetplayPackets.h"
#include "port/port.h"
#include "port/input.h"

#include <cstring>
#include <algorithm>

NetplayManager& NetplayManager::get() {
    static NetplayManager sInstance;
    return sInstance;
}

NetplayManager::NetplayManager() {
    mLastPingSent = std::chrono::steady_clock::now();
    mLastPacketReceived = std::chrono::steady_clock::now();
    mLastBeaconSent = std::chrono::steady_clock::now();
}

NetplayManager::~NetplayManager() {
    shutdown();
}

void NetplayManager::init() {
    if (mRunning.load()) return;
    mRunning.store(true);

    mDiscoverySocket.open(kDefaultPort, true);
    mWorkerThread = std::thread(&NetplayManager::workerLoop, this);
    PORT_LOG("NetplayManager: initialized and started worker thread\n");
}

void NetplayManager::shutdown() {
    if (!mRunning.load()) return;
    disconnect();

    mRunning.store(false);
    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }

    mSocket.close();
    mDiscoverySocket.close();
    PORT_LOG("NetplayManager: shut down\n");
}

void NetplayManager::update() {
    int64_t curMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lk(mMutex);
    mLanHosts.erase(
        std::remove_if(mLanHosts.begin(), mLanHosts.end(),
                       [curMs](const PortLanHostInfo& h) {
                           return (curMs - h.lastSeenMs) > 4000;
                       }),
        mLanHosts.end()
    );
}

bool NetplayManager::startHost(int port, int mode, const std::string& roomName) {
    disconnect();

    int hostPort = (port > 0) ? port : kDefaultPort;
    std::string room = roomName.empty() ? "Galaxy Room" : roomName;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mHostPort = hostPort;
        mRoomName = room;
    }
    mMode.store(mode);

    if (mDiscoverySocket.getPort() == hostPort) {
        mDiscoverySocket.close();
    }

    if (!mSocket.open(hostPort, true)) {
        PORT_LOG("NetplayManager: failed host port %d, retrying ephemeral\n", hostPort);
        if (!mSocket.open(0, true)) return false;
        hostPort = mSocket.getPort();
        std::lock_guard<std::mutex> lk(mMutex);
        mHostPort = hostPort;
    }

    mRole.store(PORT_NET_ROLE_HOST);
    mConnected.store(false);
    PORT_LOG("NetplayManager: hosting '%s' on %d (mode=%d)\n", room.c_str(), hostPort, mode);
    return true;
}

bool NetplayManager::connectTo(const std::string& ip, int port) {
    disconnect();

    int targetPort = (port > 0) ? port : kDefaultPort;
    if (!mSocket.open(0, true)) {
        PORT_LOG("NetplayManager: failed to open client socket\n");
        return false;
    }

    setPeer(ip, targetPort);
    mRole.store(PORT_NET_ROLE_CLIENT);
    mConnected.store(false);

    ConnectReqPayload req;
    std::memset(&req, 0, sizeof(req));
    std::strncpy(req.playerName, "Player 2", sizeof(req.playerName) - 1);
    req.preferredMode = (uint8_t)mMode.load();

    sendPacket(PKT_CONNECT_REQ, &req, sizeof(req), ip, targetPort);
    PORT_LOG("NetplayManager: sent connect request to %s:%d\n", ip.c_str(), targetPort);
    return true;
}

void NetplayManager::disconnect(bool stopHosting) {
    Peer peer = peerSnapshot();
    if (mRole.load() != PORT_NET_ROLE_NONE && !peer.ip.empty() && peer.port > 0) {
        sendPacket(PKT_DISCONNECT, nullptr, 0, peer.ip, peer.port);
    }

    PortPadState zeroPad;
    std::memset(&zeroPad, 0, sizeof(zeroPad));
    zeroPad.connected = 0;
    port_input_set(1, &zeroPad);

    if (stopHosting || mRole.load() == PORT_NET_ROLE_CLIENT) {
        mRole.store(PORT_NET_ROLE_NONE);
    }
    mConnected.store(false);
    setPeer(std::string(), 0);
    mPingMs.store(0);

    std::lock_guard<std::mutex> lk(mMutex);
    mHasRemotePlayer = false;
    std::memset(&mLatestRemotePlayer, 0, sizeof(mLatestRemotePlayer));
}

std::string NetplayManager::getPeerIp() const {
    return peerSnapshot().ip;
}

std::string NetplayManager::getLocalIp() const {
    return NetSocket::getLocalIpAddress();
}

int NetplayManager::getLanHosts(PortLanHostInfo* outHosts, int maxHosts) {
    std::lock_guard<std::mutex> lk(mMutex);
    int count = std::min((int)mLanHosts.size(), maxHosts);
    for (int i = 0; i < count; i++) {
        outHosts[i] = mLanHosts[i];
    }
    return count;
}

void NetplayManager::sendPlayerState(const PortRemotePlayerState& state) {
    Peer peer = peerSnapshot();
    if (!mConnected.load() || peer.ip.empty() || peer.port <= 0) return;
    sendPacket(PKT_PLAYER_STATE, &state, sizeof(state), peer.ip, peer.port);
}

bool NetplayManager::getRemotePlayerState(PortRemotePlayerState& outState) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mHasRemotePlayer) return false;
    outState = mLatestRemotePlayer;
    return true;
}

void NetplayManager::sendCostarState(const PortPadState& pad) {
    Peer peer = peerSnapshot();
    if (!mConnected.load() || peer.ip.empty() || peer.port <= 0) return;
    sendPacket(PKT_COSTAR_STATE, &pad, sizeof(pad), peer.ip, peer.port);
}

void NetplayManager::sendWarpEvent(const std::string& stage, int scenario) {
    Peer peer = peerSnapshot();
    if (!mConnected.load() || peer.ip.empty() || peer.port <= 0) return;
    EventPayload ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.eventType = 1;
    std::strncpy(ev.stage, stage.c_str(), sizeof(ev.stage) - 1);
    ev.scenario = scenario;
    sendPacket(PKT_EVENT, &ev, sizeof(ev), peer.ip, peer.port);
}

void NetplayManager::sendSpinEvent() {
    Peer peer = peerSnapshot();
    if (!mConnected.load() || peer.ip.empty() || peer.port <= 0) return;
    EventPayload ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.eventType = 2;
    sendPacket(PKT_EVENT, &ev, sizeof(ev), peer.ip, peer.port);
}

bool NetplayManager::takeWarpEvent(std::string& outStage, int& outScenario) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mPendingWarp) return false;
    outStage = mPendingWarpStage;
    outScenario = mPendingWarpScenario;
    mPendingWarp = false;
    return true;
}

bool NetplayManager::takeSpinEvent() {
    std::lock_guard<std::mutex> lk(mMutex);
    if (!mPendingSpin) return false;
    mPendingSpin = false;
    return true;
}

NetplayManager::Peer NetplayManager::peerSnapshot() const {
    std::lock_guard<std::mutex> lk(mMutex);
    return Peer{mPeerIp, mPeerPort};
}

void NetplayManager::setPeer(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lk(mMutex);
    mPeerIp = ip;
    mPeerPort = port;
}

std::string NetplayManager::roomName() const {
    std::lock_guard<std::mutex> lk(mMutex);
    return mRoomName;
}

int NetplayManager::hostPort() const {
    std::lock_guard<std::mutex> lk(mMutex);
    return mHostPort;
}
