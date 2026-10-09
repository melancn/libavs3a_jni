#include "avs3a/framer.h"
#include "avs3a/header_parser.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace avs3a;

static uint8_t make_header_byte(int bit_offset) {
    return 0;
}

static std::vector<uint8_t> make_valid_frame(int32_t bitrate, int32_t rate,
                                              int32_t channel_config, int32_t neural_type) {
    const int32_t rates[9] = {192000,96000,48000,44100,32000,24000,22050,16000,8000};
    const int32_t mono[16] = {16000,32000,44000,56000,64000,72000,80000,96000,128000,144000,164000,192000,0,0,0,0};
    const int32_t stereo[16] = {24000,32000,48000,64000,80000,96000,128000,144000,192000,256000,320000,0,0,0,0,0};

    int sr_idx = -1;
    for (int i = 0; i < 9; i++) if (rates[i] == rate) { sr_idx = i; break; }
    assert(sr_idx >= 0);

    const int32_t* br_table = (channel_config == 0) ? mono : stereo;
    int br_idx = -1;
    for (int i = 0; i < 16; i++) if (br_table[i] == bitrate) { br_idx = i; break; }
    assert(br_idx >= 0);

    volatile float ratio = (float)bitrate / (float)rate;
    volatile float total = ratio * 1024.0f;
    uint32_t frame_bits = (uint32_t)total;
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
    set_bits(17, 3, neural_type);
    set_bits(20, 3, 0);
    set_bits(23, 4, sr_idx);
    set_bits(35, 7, channel_config);
    set_bits(42, 2, 1);
    set_bits(44, 4, br_idx);

    std::vector<uint8_t> payload(payload_bytes, 0);
    for (size_t i = 0; i < payload_bytes; i++) payload[i] = (uint8_t)(i * 7 + 3);

    uint16_t crc = compute_crc16(payload.data(), payload_bytes);
    set_bits(27, 8, (crc >> 8) & 0xFF);
    set_bits(48, 8, crc & 0xFF);

    std::vector<uint8_t> frame = header;
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

int framer_test_main() {
    printf("=== Framer tests ===\n");

    {
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
    Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 0);
    assert(s == QUEUE_ACCEPTED);
    FramerResult r = framer.next();
    if (r.kind != FramerResult::Ready) {
        fprintf(stderr, "DEBUG: kind=%d error=%d reason='%s' required=%zu frame_size=%zu\n",
               (int)r.kind, (int)r.error, r.reason.c_str(), r.required_minimum, frame.size());
        fprintf(stderr, "DEBUG: frame[0]=0x%02x frame[1]=0x%02x frame[2]=0x%02x\n",
               frame.size() > 0 ? frame[0] : 0, frame.size() > 1 ? frame[1] : 0,
               frame.size() > 2 ? frame[2] : 0);
        fflush(stderr);
    }
    assert(r.kind == FramerResult::Ready);
        assert(r.frame.bytes.size() == frame.size());
        assert(r.frame.config.sample_rate == 48000);
        assert(r.frame.config.channels == 2);
        printf("  basic frame: PASS\n");
    }

    {
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        for (size_t i = 0; i < frame.size(); i++) {
            Status s = framer.queue_input(&frame[i], 1, (i == 0) ? 1000LL : TIME_UNSET, 0, 0);
            assert(s == QUEUE_ACCEPTED);
        }
        FramerResult r;
        int attempts = 0;
        do {
            r = framer.next();
            attempts++;
        } while (r.kind == FramerResult::NeedMore && attempts < 200);
        assert(r.kind == FramerResult::Ready);
        printf("  byte-by-byte: PASS\n");
    }

    {
        BoundedFramer framer;
        std::vector<uint8_t> junk = {0x00, 0x00, 0x00, 0x00};
        framer.queue_input(junk.data(), junk.size(), TIME_UNSET, 0, 0);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::NeedMore || r.kind == FramerResult::Invalid);
        framer.signal_end_of_input();
        r = framer.next();
        assert(r.kind == FramerResult::Invalid || r.kind == FramerResult::NeedMore);
        printf("  false sync / EOS: PASS\n");
    }

    {
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        std::vector<uint8_t> two_frames = frame;
        two_frames.insert(two_frames.end(), frame.begin(), frame.end());
        framer.queue_input(two_frames.data(), two_frames.size(), 1000, 0, 0);
        FramerResult r1 = framer.next();
        assert(r1.kind == FramerResult::Ready);
        r1 = framer.next();
        r1 = framer.next();
        assert(r1.kind == FramerResult::Ready);
        printf("  multi-frame: PASS\n");
    }

    {
        BoundedFramer framer;
        std::vector<uint8_t> huge(MAX_INPUT_CHUNK + 1, 0);
        Status s = framer.queue_input(huge.data(), huge.size(), TIME_UNSET, 0, 0);
        assert(s == INPUT_TOO_LARGE);
        printf("  oversized chunk: PASS\n");
    }

    printf("=== Framer tests: ALL PASS ===\n");
    return 0;
}
