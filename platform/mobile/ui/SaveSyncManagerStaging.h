// Staging and promotion of a save across the eight NAND copies. These are the
// internals of SaveSyncManager, split into their own translation unit (the same
// way TouchInputHandler splits its pointer handling) because the repository caps
// every maintained file at 300 lines. Nothing outside mobile_ui may call them,
// and nothing here touches the toast messages the two public entry points own.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mobile_ui {
namespace stage {

inline constexpr const char* const kTitleIds[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
inline constexpr const char* const kSaveNames[] = {"GameData.bin", "save.bin"};

enum class NandWriteResult { Success, NotWritten, RolledBack, RollbackIncomplete };

bool makeDirs(const std::string& path);

// A killed promotion leaves "<file>.import_rollback" and no live file.
bool recoverInterruptedImport(const std::string& saveRoot);

// The quick-save position snapshot describes where Mario stood inside the save
// it was taken from, so it must not outlive that save: an imported GameData.bin
// paired with the previous session's snapshot would drop the new save's player
// at the old coordinates and health.
bool removeMarioStates(const std::string& saveRoot);

// All eight copies are staged first, then renamed live. A failed promotion
// restores the previous files and reports whether that actually completed.
NandWriteResult writeNandSave(const std::string& saveRoot, const std::vector<uint8_t>& data);

}  // namespace stage
}  // namespace mobile_ui
