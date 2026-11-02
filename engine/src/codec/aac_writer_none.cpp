// Bản host Linux (test / CI, server): không có bộ nén AAC của hệ điều hành.

#include "codec/aac_writer.h"

namespace tonecore {

std::unique_ptr<AacWriter> createAacWriter(const std::string& /*path*/, uint32_t /*sampleRate*/,
                                           uint32_t /*channels*/, int32_t /*bitrate*/,
                                           tc_result& err) {
  err = TC_ERR_NOT_SUPPORTED;
  return nullptr;
}

}  // namespace tonecore
