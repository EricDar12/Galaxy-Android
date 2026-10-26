#pragma once

#include "gx/vk/vk_common.h"

namespace gpu {

// Encodes the canonical pipeline key for a given draw item and shader UID.
PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid);

}  // namespace gpu
