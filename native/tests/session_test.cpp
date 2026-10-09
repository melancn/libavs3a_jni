#include "avs3a/session.h"
#include "avs3a/registry.h"
#include "fake_backend.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <memory>

using namespace avs3a;

static std::vector<uint8_t> make_test_frame() {
    const int32_t rate = 48000;
    const int32_t bitrate = 128000;
    const int32_t channel_config = 1;

    const int32_t rates[9] = {192000,96000,48000,44100,32000,24000,22050,16000,8000};
    const int32_t stereo[16] = {24000,32000,48000,64000,80000,96000,128000,144000,192000,256000,320000,0,0,0,0,0};

    int sr_idx = -1;
    for (int i = 0; i < 9; i++) if (rates[i] == rate) { sr_idx = i; break; }

    int br_idx = -1;
    for (int i = 0; i < 16; i++) if (stereo[i] == bitrate) { br_idx = i; break; }

    volatile float ratio = (float)bitrate / (float)rate;
    volatile float total = ratio * 1024.0f;
    uint32_t frame_bits = (uint32_t)total;
    assert(frame_bits > 56);
    uint32_t payload_bits = frame_bits - 56;
    uint32_t payload_bytes = (payload_bits + 7) / 8;
    uint32_t frame_bytes = 7 + payload_bytes;

    std::vector<uint8_t> header(7, 0);
    auto set_bits = [&](int start, int n, uint32_t val) {
        for (int i = 0; i < n; i++) {
            int b = start + i;
            int bit = (val >> (n - 1 - i)) & 1;
            header[b / 8] |= (bit << (7 - b % 8));
        }
    };
    set_bits(0, 12, 4095);
    set_bits(12, 4, 2);
    set_bits(16, 1, 0);
    set_bits(17, 3, 0);
    set_bits(20, 3, 0);
    set_bits(23, 4, sr_idx);
    set_bits(35, 7, channel_config);
    set_bits(42, 2, 1);
    set_bits(44, 4, br_idx);

    std::vector<uint8_t> payload(payload_bytes, 0xAB);
    uint16_t crc = compute_crc16(payload.data(), payload_bytes);
    set_bits(27, 8, (crc >> 8) & 0xFF);
    set_bits(48, 8, crc & 0xFF);

    std::vector<uint8_t> frame = header;
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

int session_test_main() {
    printf("=== Session tests ===\n");

    {
        auto backend = std::make_unique<FakeBackend>();
        Session session(HandleKind::DECODER, std::move(backend));
        session.set_epoch(100);

        auto frame = make_test_frame();
        Status s = session.queue_input(frame.data(), frame.size(), 1000, 100, 0);
        assert(s == QUEUE_ACCEPTED);

        std::vector<uint8_t> out(4096);
        PcmMetadata meta{};
        s = session.receive(MutableByteSpan(out.data(), out.size()), meta);
        assert(s == RECEIVE_READY);
        assert(meta.channels == 2);
        assert(meta.sample_rate == 48000);
        printf("  basic decode: PASS\n");
    }

    {
        auto backend = std::make_unique<FakeBackend>();
        Session session(HandleKind::DECODER, std::move(backend));
        session.set_epoch(1);
        session.close();
        Status s = session.queue_input(nullptr, 0, 0, 1, 0);
        assert(s == CLOSED_OR_INVALID_HANDLE);
        printf("  closed session: PASS\n");
    }

    {
        auto backend = std::make_unique<FakeBackend>();
        Session session(HandleKind::DECODER, std::move(backend));
        session.set_epoch(1);
        Status s = session.queue_input(nullptr, 0, 0, 0, 0);
        assert(s == STALE_EPOCH);
        printf("  stale epoch: PASS\n");
    }

    {
        auto backend = std::make_unique<FakeBackend>();
        Session session(HandleKind::DECODER, std::move(backend));
        session.set_epoch(1);
        auto frame = make_test_frame();
        session.queue_input(frame.data(), frame.size(), 1000, 1, 0);

        std::vector<uint8_t> small(10);
        PcmMetadata meta{};
        Status s = session.receive(MutableByteSpan(small.data(), small.size()), meta);
        assert(s == RECEIVE_OUTPUT_TOO_SMALL);
        printf("  output too small: PASS\n");
    }

    {
        auto fb = std::make_unique<FakeBackend>();
        FakeBackend* raw = fb.get();
        Session session(HandleKind::DECODER, std::move(fb));
        session.set_epoch(1);
        session.close();
        assert(raw->destroy_count() == 1);
        printf("  destroy on close: PASS\n");
    }

    printf("=== Session tests: ALL PASS ===\n");
    return 0;
}
