#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <RVLFaceLib.h>

namespace {
    /// @brief The array of message identifiers corresponding to File Selection Screen icon names.
    static const char* sIconNameMessageID[] = {
        "System_FileSelect_Icon000", "System_FileSelect_Icon001", "System_FileSelect_Icon002",
        "System_FileSelect_Icon003", "System_FileSelect_Icon004",
    };
};  // namespace

namespace FileSelectFunc {
    u32 getMiiNameBufferSize() {
        return RFL_NAME_LEN + 1;
    }

    void copyMiiName(u16* pName, const FileSelectIconID& rIcon) {
        if (!pName) {
            return;
        }
        const u32 bufChars = getMiiNameBufferSize();
        pName[0] = 0;
        pName[bufChars - 1] = 0;

        if (rIcon.isFellow()) {
            s32 fellowId = rIcon.getFellowID();
            if (fellowId >= 0 && fellowId < static_cast< s32 >(sizeof(::sIconNameMessageID) / sizeof(::sIconNameMessageID[0]))) {
                const char* pMessageId = ::sIconNameMessageID[fellowId];
                const wchar_t* pMessage = pMessageId ? MR::getGameMessageDirect(pMessageId) : nullptr;
                if (pMessage) {
                    MR::copyMemory(pName, pMessage, bufChars * sizeof(wchar_t));
                    pName[bufChars - 1] = 0;
                    return;
                }
            }
        } else if (rIcon.isMii()) {
            RFLAdditionalInfo info;
            RFLErrcode err = RFLGetAdditionalInfo(&info, RFLDataSource_Official, nullptr, rIcon.getMiiIndex());

            if (err == RFLErrcode_Success && info.name[0] != 0) {
                MR::copyMemory(pName, info.name, bufChars * sizeof(wchar_t));
                pName[bufChars - 1] = 0;
                return;
            }
        }

        const wchar_t* pDefault = MR::getGameMessageDirect("System_FileSelect_Mario");
        if (pDefault) {
            MR::copyMemory(pName, pDefault, bufChars * sizeof(wchar_t));
        } else {
            static const u16 sFallbackName[] = {'M', 'a', 'r', 'i', 'o', 0};
            for (u32 i = 0; i < bufChars && i < sizeof(sFallbackName) / sizeof(sFallbackName[0]); ++i) {
                pName[i] = sFallbackName[i];
            }
        }
        pName[bufChars - 1] = 0;
    }
};  // namespace FileSelectFunc
