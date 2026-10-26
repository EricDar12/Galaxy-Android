#include "Settings.h"
#include "SettingsCodec.h"

#include <android/log.h>
#include <stdio.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

void Settings::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        applySettingLine(*this, line);
    }
    fclose(f);
    LOGI("Loaded settings from %s", path.c_str());
}

void Settings::save(const std::string& path) const {
    // Write beside the destination and rename, so a kill mid-save cannot leave
    // a truncated settings file behind.
    const std::string tmpPath = path + ".tmp";
    const std::string text = serializeSettings(*this);
    FILE* f = fopen(tmpPath.c_str(), "w");
    if (!f) return;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmpPath.c_str(), path.c_str()) != 0) {
        remove(tmpPath.c_str());
        LOGI("Failed to save settings atomically to %s", path.c_str());
        return;
    }
    LOGI("Saved settings to %s", path.c_str());
}

}  // namespace mobile_ui
