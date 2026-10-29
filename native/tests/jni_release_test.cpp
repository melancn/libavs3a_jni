#include "avs3a/backend.h"
#include "avs3a/status.h"
#include <jni.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

extern "C" jint JNI_OnLoad(JavaVM*, void*);

namespace {
std::atomic<int> destroyed_backends{0};
const JNINativeMethod* registered_methods = nullptr;
jint registered_method_count = 0;
JNIEnv* test_env = nullptr;

class ReleaseBackend final : public avs3a::DecoderBackend {
public:
    avs3a::Status initialize(const avs3a::FrameConfig&, const avs3a::VerifiedModelPath&) override {
        return avs3a::OK;
    }
    avs3a::Status decode(const avs3a::EncodedFrame&, avs3a::OwnedPcm&) override {
        return avs3a::INVALID_STATE;
    }
    void destroy() noexcept override {
        if (!destroyed_.exchange(true)) ++destroyed_backends;
    }
    bool is_ready() const override { return true; }
private:
    std::atomic<bool> destroyed_{false};
};

jint JNICALL get_env(JavaVM*, void** output, jint version) {
    assert(version == JNI_VERSION_1_6);
    *output = test_env;
    return JNI_OK;
}
jclass JNICALL find_class(JNIEnv*, const char* name) {
    assert(std::strcmp(name, "com/inlz/avs3a/NativeBridge") == 0);
    return reinterpret_cast<jclass>(1);
}
jint JNICALL register_natives(JNIEnv*, jclass, const JNINativeMethod* methods, jint count) {
    registered_methods = methods;
    registered_method_count = count;
    return JNI_OK;
}
void JNICALL delete_local_ref(JNIEnv*, jobject) {}

template <typename Function>
Function callback(const char* name) {
    for (jint i = 0; i < registered_method_count; ++i) {
        if (std::strcmp(registered_methods[i].name, name) == 0)
            return reinterpret_cast<Function>(registered_methods[i].fnPtr);
    }
    assert(false && "JNI callback must be registered by the production bridge");
    return nullptr;
}

using CreateDecoder = jlong (*)(JNIEnv*, jclass, jstring, jstring, jlong);
using CreateParser = jlong (*)(JNIEnv*, jclass, jlong);
using Release = void (*)(JNIEnv*, jclass, jlong);
using End = jint (*)(JNIEnv*, jclass, jlong);

void exercise_release(const char* mode, JNIEnv* env) {
    const bool decoder = std::strcmp(mode, "decoder") == 0;
    auto release = callback<Release>(decoder ? "nRelease" : "nParserRelease");
    auto end = callback<End>(decoder ? "nEnd" : "nParserEnd");
    auto create_decoder = callback<CreateDecoder>("nCreate");
    auto create_parser = callback<CreateParser>("nParserCreate");
    auto create = [&]() {
        return decoder ? create_decoder(env, nullptr, nullptr, nullptr, 1)
                       : create_parser(env, nullptr, 1);
    };

    std::printf("=== Actual JNI %s release: 100 cycles ===\n", mode);
    std::fflush(stdout);
    for (int i = 0; i < 100; ++i) {
        const int before = destroyed_backends.load();
        const jlong handle = create();
        assert(handle > 0);
        release(env, nullptr, handle);
        assert(end(env, nullptr, handle) == avs3a::CLOSED_OR_INVALID_HANDLE);
        release(env, nullptr, handle);
        assert(destroyed_backends.load() == before + (decoder ? 1 : 0));
    }

    // JNI need not rely on the Java synchronized wrapper to handle duplicate releases.
    const int before = destroyed_backends.load();
    const jlong handle = create();
    std::vector<std::thread> releasers;
    for (int i = 0; i < 8; ++i)
        releasers.emplace_back([&]() { release(env, nullptr, handle); });
    for (auto& thread : releasers) thread.join();
    assert(end(env, nullptr, handle) == avs3a::CLOSED_OR_INVALID_HANDLE);
    assert(destroyed_backends.load() == before + (decoder ? 1 : 0));

    release(env, nullptr, 0);
    release(env, nullptr, -1);
    release(env, nullptr, static_cast<jlong>(1) << 60);
    std::printf("%s: repeated/concurrent release, invalid handles, single destroy: PASS\n", mode);
}
} // namespace

// Only this standalone test target substitutes the vendor loader/build identity.
// The production JNI registration table and both release callbacks are unchanged.
namespace avs3a {
std::unique_ptr<DecoderBackend> load_vendor_backend(const char*, int process_abi) {
    assert(process_abi == 1 || process_abi == 2);
    return std::make_unique<ReleaseBackend>();
}
} // namespace avs3a
extern "C" const char* avs3a_get_build_info_cstr() { return "jni-release-test"; }
extern "C" int avs3a_get_process_abi() { return 1; }

int main(int argc, char** argv) {
    using EnvFunctions = std::remove_const_t<std::remove_pointer_t<decltype(JNIEnv{}.functions)>>;
    using VmFunctions = std::remove_const_t<std::remove_pointer_t<decltype(JavaVM{}.functions)>>;
    EnvFunctions env_functions{};
    env_functions.FindClass = find_class;
    env_functions.RegisterNatives = register_natives;
    env_functions.DeleteLocalRef = delete_local_ref;
    JNIEnv env{};
    env.functions = &env_functions;
    test_env = &env;
    VmFunctions vm_functions{};
    vm_functions.GetEnv = get_env;
    JavaVM vm{};
    vm.functions = &vm_functions;
    assert(JNI_OnLoad(&vm, nullptr) == JNI_VERSION_1_6);

    const char* mode = argc > 1 ? argv[1] : "both";
    assert(std::strcmp(mode, "decoder") == 0 || std::strcmp(mode, "parser") == 0 ||
           std::strcmp(mode, "both") == 0);
    if (std::strcmp(mode, "parser") != 0) exercise_release("decoder", &env);
    if (std::strcmp(mode, "decoder") != 0) exercise_release("parser", &env);
    std::puts("JNI RELEASE TESTS PASSED");
    return 0;
}
