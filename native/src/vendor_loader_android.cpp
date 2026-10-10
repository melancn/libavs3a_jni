#include "avs3a/backend.h"
#include "vendor_api.h"
#include <dlfcn.h>
#include <memory>

namespace avs3a {

std::unique_ptr<DecoderBackend> load_vendor_backend(const char* path, int process_abi) {
    if (!path || !*path) return nullptr;

    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return nullptr;

    VendorApi api{};
    api.alloc = reinterpret_cast<Avs3AllocFn>(dlsym(handle, "Avs3AllocDecoder"));
    api.init = reinterpret_cast<Avs3InitFn>(dlsym(handle, "Avs3InitDecoder"));
    api.decode = reinterpret_cast<Avs3DecodeFn>(dlsym(handle, "Avs3Decode"));
    api.destroy = reinterpret_cast<Avs3DestroyFn>(dlsym(handle, "Avs3DecoderDestroy"));
    api.reset_bitstream = reinterpret_cast<Avs3ResetFn>(dlsym(handle, "ResetBitstream"));

    if (!api.alloc || !api.init || !api.decode || !api.destroy || !api.reset_bitstream) {
        dlclose(handle);
        return nullptr;
    }

    return create_vendor_backend(process_abi, handle, api);
}

} // namespace avs3a
