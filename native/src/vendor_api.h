#ifndef AVS3A_VENDOR_API_H
#define AVS3A_VENDOR_API_H

#include "avs3a/backend.h"
#include <cstdint>
#include <memory>

namespace avs3a {

typedef void* (*Avs3AllocFn)(void);
typedef void (*Avs3InitFn)(void*, const char*);
typedef void (*Avs3DecodeFn)(void*, int16_t*);
typedef void (*Avs3DestroyFn)(void*);
typedef void (*Avs3ResetFn)(void*);

struct VendorApi {
    Avs3AllocFn alloc             = nullptr;
    Avs3InitFn init               = nullptr;
    Avs3DecodeFn decode           = nullptr;
    Avs3DestroyFn destroy         = nullptr;
    Avs3ResetFn reset_bitstream   = nullptr;
};

std::unique_ptr<DecoderBackend> create_vendor_backend(int process_abi, void* lib_handle,
                                                      const VendorApi& api);

std::unique_ptr<DecoderBackend> load_vendor_backend(const char* path, int process_abi);

} // namespace avs3a

#endif // AVS3A_VENDOR_API_H