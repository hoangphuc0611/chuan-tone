// Bản host (macOS/Linux dùng cho test & CI): chưa có thiết bị âm thanh thật.
// Engine vẫn chạy được với TC_BACKEND_NULL qua tc_engine_process_offline.

#include "io/audio_backend.h"

namespace tonecore {

std::unique_ptr<AudioBackend> createPlatformBackend(BackendHost& /*host*/,
                                                    const EngineConfig& /*config*/) {
  return nullptr;
}

}  // namespace tonecore
