#include "avs3a/framer.h"
#include "avs3a/header_parser.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace avs3a;

static std::vector<uint8_t> make_frame_with_table(const int32_t* br_table, int32_t bitrate,
                                                    int32_t rate, int32_t channel_config,
                                                    int32_t neural_type);

static std::vector<uint8_t> make_valid_frame(int32_t bitrate, int32_t rate,
                                              int32_t channel_config, int32_t neural_type) {
    const int32_t mono[16] = {16000,32000,44000,56000,64000,72000,80000,96000,128000,144000,164000,192000,0,0,0,0};
    const int32_t stereo[16] = {24000,32000,48000,64000,80000,96000,128000,144000,192000,256000,320000,0,0,0,0,0};
    const int32_t* br_table = (channel_config == 0) ? mono : stereo;
    return make_frame_with_table(br_table, bitrate, rate, channel_config, neural_type);
}

static std::vector<uint8_t> make_frame_with_table(const int32_t* br_table, int32_t bitrate,
                                                    int32_t rate, int32_t channel_config,
                                                    int32_t neural_type) {
    int sr_idx = -1;
    for (int i = 0; i < 9; i++) if (kSampleRateIndexTable[i] == rate) { sr_idx = i; break; }
    assert(sr_idx >= 0);

    int br_idx = -1;
    for (int i = 0; i < 16; i++) if (br_table[i] == bitrate) { br_idx = i; break; }
    assert(br_idx >= 0);

    volatile float ratio = (float)bitrate / (float)rate;
    volatile float total = ratio * 1024.0f;
    uint32_t frame_bits = (uint32_t)total;
    assert(frame_bits > 56);
    uint32_t payload_bits = frame_bits - 56;
    uint32_t payload_bytes = (payload_bits + 7) / 8;

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

static void set_header_bits(std::vector<uint8_t>& header, int start, int n, uint32_t val) {
    for (int i = 0; i < n; i++) {
        int b = start + i;
        int bit = (val >> (n - 1 - i)) & 1;
        header[b / 8] = (uint8_t)((header[b / 8] & ~(1 << (7 - b % 8))) | (bit << (7 - b % 8)));
    }
}

// Header-only frame for configs the parser must reject before any bitrate
// lookup (unsupported channel configs); payload content is irrelevant.
static std::vector<uint8_t> make_raw_frame(int32_t rate, int32_t channel_config) {
    int sr_idx = -1;
    for (int i = 0; i < 9; i++) if (kSampleRateIndexTable[i] == rate) { sr_idx = i; break; }
    assert(sr_idx >= 0);

    std::vector<uint8_t> frame(7 + 64, 0);
    set_header_bits(frame, 0, 12, 4095);
    set_header_bits(frame, 12, 4, 2);
    set_header_bits(frame, 16, 1, 0);
    set_header_bits(frame, 17, 3, 0);
    set_header_bits(frame, 20, 3, 0);
    set_header_bits(frame, 23, 4, (uint32_t)sr_idx);
    set_header_bits(frame, 35, 7, (uint32_t)channel_config);
    set_header_bits(frame, 42, 2, 1);
    set_header_bits(frame, 44, 4, 0);
    for (size_t i = 7; i < frame.size(); i++) frame[i] = (uint8_t)(i * 7 + 3);
    uint16_t crc = compute_crc16(frame.data() + 7, frame.size() - 7);
    set_header_bits(frame, 27, 8, (crc >> 8) & 0xFF);
    set_header_bits(frame, 48, 8, crc & 0xFF);
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
        assert(r1.kind == FramerResult::Ready);
        r1 = framer.next();
        assert(r1.kind == FramerResult::NeedMore);
        printf("  multi-frame: PASS\n");
    }

    {
        BoundedFramer framer;
        std::vector<uint8_t> huge(MAX_INPUT_CHUNK + 1, 0);
        Status s = framer.queue_input(huge.data(), huge.size(), TIME_UNSET, 0, 0);
        assert(s == INPUT_TOO_LARGE);
        printf("  oversized chunk: PASS\n");
    }

    {
        // MC 5.1 (channelConfig 2): bitrateTableMC5P1[3] = 384000 @ 48 kHz,
        // the target MP4 configuration.
        BoundedFramer framer;
        auto frame = make_frame_with_table(kBitrateIndexTable_MC5P1, 384000, 48000, 2, 0);
        Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 0);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Ready);
        assert(r.frame.config.channels == 6);
        assert(r.frame.config.channel_config == 2);
        assert(r.frame.config.mode == CHANNEL_MODE_MC);
        assert(r.frame.config.layout_id == 3);
        assert(r.frame.config.vendor_fields.channel_count == 6);
        assert(r.frame.config.vendor_fields.lfe_flag == 1);
        assert(r.frame.config.vendor_fields.decoder_format == 2);
        // 384000/48000*1024 = 8192 bits -> payload 8136 bits -> 1017 bytes
        assert(r.frame.config.frame_bytes == 7 + 1017);
        assert(r.frame.bytes.size() == frame.size());
        printf("  MC 5.1 frame: PASS\n");
    }

    {
        // MC 4.0 (channelConfig 6): the only MC layout without LFE.
        BoundedFramer framer;
        auto frame = make_frame_with_table(kBitrateIndexTable_MC4P0, 128000, 48000, 6, 0);
        Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 0);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Ready);
        assert(r.frame.config.channels == 4);
        assert(r.frame.config.layout_id == 7);
        assert(r.frame.config.mode == CHANNEL_MODE_MC);
        assert(r.frame.config.vendor_fields.lfe_flag == 0);
        assert(r.frame.config.vendor_fields.decoder_format == 2);
        printf("  MC 4.0 frame (no LFE): PASS\n");
    }

    {
        // channelConfig 4 (MC_10_2): representable layout, NULL vendor
        // bitrate table -> must stay unsupported.
        BoundedFramer framer;
        auto frame = make_raw_frame(48000, 4);
        Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 0);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Invalid);
        assert(r.error == UNSUPPORTED_MODE);
        printf("  MC_10_2 rejected (no vendor table): PASS\n");
    }

    {
        // channelConfig 11: outside the vendor's channel-based range (b.hi #10).
        BoundedFramer framer;
        auto frame = make_raw_frame(48000, 11);
        Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 0);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Invalid);
        assert(r.error == UNSUPPORTED_MODE);
        printf("  channelConfig 11 rejected: PASS\n");
    }

    {
        // INPUT_COMPLETE_SINGLE_FRAME: exactly one frame is accepted.
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        Status s = framer.queue_input(frame.data(), frame.size(), 1000, 0, 1);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Ready);
        printf("  single-frame flag accepted: PASS\n");
    }

    {
        // INPUT_COMPLETE_SINGLE_FRAME: trailing bytes violate the contract.
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        std::vector<uint8_t> padded = frame;
        padded.push_back(0x00);
        Status s = framer.queue_input(padded.data(), padded.size(), 1000, 0, 1);
        assert(s == COMPLETE_SAMPLE_CONTRACT);
        printf("  single-frame flag rejects trailing bytes: PASS\n");
    }

    {
        // INPUT_COMPLETE_SINGLE_FRAME: incomplete frame violates the contract.
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        Status s = framer.queue_input(frame.data(), frame.size() / 2, 1000, 0, 1);
        assert(s == COMPLETE_SAMPLE_CONTRACT);
        printf("  single-frame flag rejects incomplete frame: PASS\n");
    }

    {
        // INPUT_COMPLETE_SINGLE_FRAME: split queueing is fine when the final
        // chunk completes exactly one frame.
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        Status s = framer.queue_input(frame.data(), 10, 1000, 0, 0);
        assert(s == QUEUE_ACCEPTED);
        s = framer.queue_input(frame.data() + 10, frame.size() - 10, 1000, 0, 1);
        assert(s == QUEUE_ACCEPTED);
        FramerResult r = framer.next();
        assert(r.kind == FramerResult::Ready);
        printf("  single-frame flag split queue: PASS\n");
    }

    {
        BoundedFramer framer;
        auto frame = make_valid_frame(128000, 48000, 1, 0);
        assert(framer.queue_input(frame.data(), frame.size(), 1000, 0, 1) == QUEUE_ACCEPTED);
        assert(framer.queue_input(frame.data(), frame.size(), 90000, 0, 1) == QUEUE_ACCEPTED);
        assert(framer.next().frame.pts_us == 1000);
        assert(framer.next().frame.pts_us == 90000);
        framer.flush();
        assert(framer.queue_input(frame.data(), frame.size(), TIME_UNSET, 1, 1) == QUEUE_ACCEPTED);
        assert(framer.next().frame.pts_us == TIME_UNSET);
        framer.signal_end_of_input();
        assert(framer.queue_input(frame.data(), frame.size(), 0, 1, 1) == INVALID_STATE);
        printf("  queued sample timestamps / unset after flush / EOS: PASS\n");
    }

    printf("=== Framer tests: ALL PASS ===\n");
    return 0;

}
