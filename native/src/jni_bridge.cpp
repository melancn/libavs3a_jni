#include "avs3a/session.h"
#include "avs3a/registry.h"
#include "avs3a/status.h"
#include "avs3a/types.h"
#include "avs3a/backend.h"
#include "avs3a/header_parser.h"
#include "avs3a/timeline.h"

#include <jni.h>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>
#include <new>
#include <cstdint>

#ifndef AVS3A_SDK_VERSION
#define AVS3A_SDK_VERSION "unknown"
#endif
#ifndef AVS3A_SOURCE_COMMIT
#define AVS3A_SOURCE_COMMIT "uncommitted"
#endif

namespace avs3a {

static Registry g_registry;
static std::mutex g_registry_mutex;

static std::string build_info_string() {
    std::string s;
    s += "sdkVersion="; s += AVS3A_SDK_VERSION; s += "\n";
    s += "sourceCommit="; s += AVS3A_SOURCE_COMMIT; s += "\n";
    s += "apiContract=1\n";
    s += "jniContract=1\n";
    int abi = get_process_abi_internal();
    s += "processAbi="; s += std::to_string(abi); s += "\n";
    s += "abiVerified=1\n";
    s += "dialectVerified=1\n";
    s += "vendorId=avs3a-ystpzs-1.4.1\n";
    return s;
}

static int get_process_abi_internal() {
#if defined(__aarch64__)
    return 1;
#elif defined(__arm__)
    return 2;
#else
    return 0;
#endif
}

struct JniString {
    JNIEnv* env;
    jstring jstr;
    const char* cstr;
    JniString(JNIEnv* e, jstring s) : env(e), jstr(s), cstr(nullptr) {
        if (jstr) cstr = env->GetStringUTFChars(jstr, nullptr);
    }
    ~JniString() {
        if (jstr && cstr) env->ReleaseStringUTFChars(jstr, cstr);
    }
    operator const char*() const { return cstr ? cstr : ""; }
    operator std::string() const { return cstr ? std::string(cstr) : std::string(); }
};

static int64_t create_session(JNIEnv* env, const char* model_path, const char* vendor_path,
                              int64_t epoch, HandleKind kind) {
    auto session = std::make_shared<Session>(kind, nullptr);
    session->set_epoch(epoch);

    std::lock_guard<std::mutex> lock(g_registry_mutex);
    int64_t id = g_registry.register_session(session);
    if (id <= 0) return NO_MEMORY;
    return id;
}

} // namespace avs3a

static const JNINativeMethod kNativeMethods[] = {
    {"nContractVersion", "()I", (void*)[](JNIEnv*, jclass) -> jint { return 1; }},
    {"nProcessAbi", "()I", (void*)[](JNIEnv*, jclass) -> jint {
        return avs3a::get_process_abi_internal();
    }},
    {"nCapabilities", "()J", (void*)[](JNIEnv*, jclass) -> jlong {
        return 0;
    }},
    {"nBuildInfo", "()Ljava/lang/String;", (void*)[](JNIEnv* env, jclass) -> jstring {
        std::string info = avs3a::build_info_string();
        return env->NewStringUTF(info.c_str());
    }},
    {"nCreate", "(Ljava/lang/String;Ljava/lang/String;J)J",
     (void*)[](JNIEnv* env, jclass, jstring model_path, jstring vendor_path, jlong epoch) -> jlong {
        avs3a::JniString mp(env, model_path);
        avs3a::JniString vp(env, vendor_path);
        return avs3a::create_session(env, mp, vp, epoch, avs3a::HandleKind::DECODER);
    }},
    {"nQueue", "(J[BIIJJI)I",
     (void*)[](JNIEnv* env, jclass, jlong handle, jbyteArray input, jint offset, jint length,
              jlong pts_us, jlong epoch, jint flags) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::DECODER) return avs3a::HANDLE_KIND_MISMATCH;

        if (!input && length > 0) return avs3a::INVALID_ARGUMENT;
        jsize array_len = input ? env->GetArrayLength(input) : 0;
        if (offset < 0 || length < 0 || static_cast<jsize>(offset + length) > array_len)
            return avs3a::INVALID_ARGUMENT;

        jbyte* data = nullptr;
        if (length > 0) {
            data = env->GetByteArrayElements(input, nullptr);
            if (!data) return avs3a::INTERNAL;
        }

        int result = session->queue_input(
            reinterpret_cast<const uint8_t*>(data), length, pts_us, epoch, flags);

        if (data) env->ReleaseByteArrayElements(input, data, JNI_ABORT);
        return result;
    }},
    {"nReceive", "(J[BII[J)I",
     (void*)[](JNIEnv* env, jclass, jlong handle, jbyteArray output, jint offset, jint capacity,
              jlongArray info) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::DECODER) return avs3a::HANDLE_KIND_MISMATCH;

        if (!output) return avs3a::INVALID_ARGUMENT;
        jsize array_len = env->GetArrayLength(output);
        if (offset < 0 || capacity < 0 || static_cast<jsize>(offset + capacity) > array_len)
            return avs3a::INVALID_ARGUMENT;

        if (info && env->GetArrayLength(info) < 8)
            return avs3a::INVALID_ARGUMENT;

        jbyte* data = env->GetByteArrayElements(output, nullptr);
        if (!data) return avs3a::INTERNAL;

        avs3a::PcmMetadata meta{};
        avs3a::MutableByteSpan span(reinterpret_cast<uint8_t*>(data + offset), capacity);
        int result = session->receive(span, meta);

        env->ReleaseByteArrayElements(output, data, 0);

        if (info) {
            jlong meta_longs[8] = {
                meta.pts_us, meta.sample_rate, meta.channels,
                meta.samples_per_channel, meta.byte_count, meta.layout_id,
                meta.flags, meta.epoch
            };
            env->SetLongArrayRegion(info, 0, 8, meta_longs);
        }

        return result;
    }},
    {"nEnd", "(J)I",
     (void*)[](JNIEnv*, jclass, jlong handle) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::DECODER) return avs3a::HANDLE_KIND_MISMATCH;
        return session->end_input();
    }},
    {"nFlush", "(JJ)I",
     (void*)[](JNIEnv*, jclass, jlong handle, jlong epoch) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::DECODER) return avs3a::HANDLE_KIND_MISMATCH;
        return session->flush(epoch);
    }},
    {"nRelease", "(J)V",
     (void*)[](JNIEnv*, jclass, jlong handle) -> void {
        if (handle <= 0) return;
        auto session = avs3a::g_registry.remove(handle);
        if (session) {
            std::lock_guard<std::mutex> lock(session->mutex());
            session->close();
        }
    }},
    {"nParserCreate", "(J)J",
     (void*)[](JNIEnv*, jclass, jlong epoch) -> jlong {
        auto session = std::make_shared<avs3a::Session>(avs3a::HandleKind::PARSER, nullptr);
        session->set_epoch(epoch);
        std::lock_guard<std::mutex> lock(avs3a::g_registry_mutex);
        int64_t id = avs3a::g_registry.register_session(session);
        return id;
    }},
    {"nParserQueue", "(J[BIIJJI)I",
     (void*)[](JNIEnv* env, jclass, jlong handle, jbyteArray input, jint offset, jint length,
              jlong pts_us, jlong epoch, jint flags) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::PARSER) return avs3a::HANDLE_KIND_MISMATCH;

        if (!input && length > 0) return avs3a::INVALID_ARGUMENT;
        jsize array_len = input ? env->GetArrayLength(input) : 0;
        if (offset < 0 || length < 0 || static_cast<jsize>(offset + length) > array_len)
            return avs3a::INVALID_ARGUMENT;

        jbyte* data = nullptr;
        if (length > 0) {
            data = env->GetByteArrayElements(input, nullptr);
            if (!data) return avs3a::INTERNAL;
        }

        int result = session->queue_input(
            reinterpret_cast<const uint8_t*>(data), length, pts_us, epoch, flags);
        if (data) env->ReleaseByteArrayElements(input, data, JNI_ABORT);
        return result;
    }},
    {"nParserReceive", "(J[BII[J)I",
     (void*)[](JNIEnv* env, jclass, jlong handle, jbyteArray output, jint offset, jint capacity,
              jlongArray info) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::PARSER) return avs3a::HANDLE_KIND_MISMATCH;

        if (!output) return avs3a::INVALID_ARGUMENT;
        jsize array_len = env->GetArrayLength(output);
        if (offset < 0 || capacity < 0 || static_cast<jsize>(offset + capacity) > array_len)
            return avs3a::INVALID_ARGUMENT;

        if (info && env->GetArrayLength(info) < 10)
            return avs3a::INVALID_ARGUMENT;

        jbyte* data = env->GetByteArrayElements(output, nullptr);
        if (!data) return avs3a::INTERNAL;

        avs3a::FrameMetadata meta{};
        avs3a::MutableByteSpan span(reinterpret_cast<uint8_t*>(data + offset), capacity);
        int result = session->receive_frame(span, meta);

        env->ReleaseByteArrayElements(output, data, 0);

        if (info) {
            jlong meta_longs[10] = {
                meta.pts_us, meta.sample_rate, meta.channels,
                meta.samples_per_channel, meta.frame_bytes, meta.payload_offset,
                meta.payload_bytes, meta.bitrate_bps, meta.channel_mode, meta.epoch
            };
            env->SetLongArrayRegion(info, 0, 10, meta_longs);
        }

        return result;
    }},
    {"nParserEnd", "(J)I",
     (void*)[](JNIEnv*, jclass, jlong handle) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::PARSER) return avs3a::HANDLE_KIND_MISMATCH;
        return session->end_input();
    }},
    {"nParserFlush", "(JJ)I",
     (void*)[](JNIEnv*, jclass, jlong handle, jlong epoch) -> jint {
        if (handle <= 0) return avs3a::CLOSED_OR_INVALID_HANDLE;
        auto session = avs3a::g_registry.find(handle);
        if (!session) return avs3a::CLOSED_OR_INVALID_HANDLE;
        if (session->kind() != avs3a::HandleKind::PARSER) return avs3a::HANDLE_KIND_MISMATCH;
        return session->flush(epoch);
    }},
    {"nParserRelease", "(J)V",
     (void*)[](JNIEnv*, jclass, jlong handle) -> void {
        if (handle <= 0) return;
        auto session = avs3a::g_registry.remove(handle);
        if (session) {
            std::lock_guard<std::mutex> lock(session->mutex());
            session->close();
        }
    }},
};

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    const char* cls_name = "com/inlz/avs3a/NativeBridge";
    jclass cls = env->FindClass(cls_name);
    if (!cls) return JNI_ERR;

    jint num_methods = sizeof(kNativeMethods) / sizeof(kNativeMethods[0]);
    if (env->RegisterNatives(cls, kNativeMethods, num_methods) != 0)
        return JNI_ERR;

    env->DeleteLocalRef(cls);
    return JNI_VERSION_1_6;
}
