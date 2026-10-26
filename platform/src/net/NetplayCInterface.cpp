#include "port/net.h"
#include "net/NetplayManager.h"

#include <cstring>

extern "C" {

void port_net_init(void) {
    NetplayManager::get().init();
}

void port_net_shutdown(void) {
    NetplayManager::get().shutdown();
}

void port_net_update(void) {
    NetplayManager::get().update();
}

int port_net_start_host(int port, int mode, const char* roomName) {
    std::string name = roomName ? roomName : "Galaxy Room";
    return NetplayManager::get().startHost(port, mode, name) ? 1 : 0;
}

int port_net_connect(const char* ip, int port) {
    if (!ip) return 0;
    return NetplayManager::get().connectTo(ip, port) ? 1 : 0;
}

void port_net_disconnect(void) {
    NetplayManager::get().disconnect(true);
}

int port_net_is_connected(void) {
    return NetplayManager::get().isConnected() ? 1 : 0;
}

int port_net_get_role(void) {
    return NetplayManager::get().getRole();
}

int port_net_get_mode(void) {
    return NetplayManager::get().getMode();
}

void port_net_set_mode(int mode) {
    NetplayManager::get().setMode(mode);
}

int port_net_get_ping_ms(void) {
    return NetplayManager::get().getPingMs();
}

const char* port_net_get_peer_ip(void) {
    static char buf[64];
    std::string ip = NetplayManager::get().getPeerIp();
    std::strncpy(buf, ip.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* port_net_get_local_ip(void) {
    static char buf[64];
    std::string ip = NetplayManager::get().getLocalIp();
    std::strncpy(buf, ip.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

int port_net_get_lan_hosts(PortLanHostInfo* outHosts, int maxHosts) {
    return NetplayManager::get().getLanHosts(outHosts, maxHosts);
}

void port_net_send_player_state(const PortRemotePlayerState* state) {
    if (state) {
        NetplayManager::get().sendPlayerState(*state);
    }
}

int port_net_get_remote_player_state(PortRemotePlayerState* outState) {
    if (!outState) return 0;
    return NetplayManager::get().getRemotePlayerState(*outState) ? 1 : 0;
}

void port_net_send_costar_state(const struct PortPadState* pad) {
    if (pad) {
        NetplayManager::get().sendCostarState(*pad);
    }
}

void port_net_send_warp_event(const char* stage, int scenario) {
    if (stage) {
        NetplayManager::get().sendWarpEvent(stage, scenario);
    }
}

void port_net_send_spin_event(void) {
    NetplayManager::get().sendSpinEvent();
}

int port_net_take_warp_event(char* outStage, size_t maxLen, int* outScenario) {
    if (!outStage || maxLen == 0 || !outScenario) return 0;
    std::string stage;
    int sc = 1;
    if (NetplayManager::get().takeWarpEvent(stage, sc)) {
        std::strncpy(outStage, stage.c_str(), maxLen - 1);
        outStage[maxLen - 1] = '\0';
        *outScenario = sc;
        return 1;
    }
    return 0;
}

int port_net_take_spin_event(void) {
    return NetplayManager::get().takeSpinEvent() ? 1 : 0;
}

} // extern "C"
