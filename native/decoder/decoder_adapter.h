/* Safe, bounded adapter around the vendored av3adecoder library.
 *
 * This is a C interface so that the C++ SDK does not include the vendor's
 * private headers. All inputs are validated here; no vendor type or raw
 * pointer is exposed to the JNI/core layers.
 */
#ifndef AVS3A_DECODER_ADAPTER_H
#define AVS3A_DECODER_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct avs3a_source_decoder avs3a_source_decoder;

enum avs3a_source_status {
    AVS3A_SRC_OK = 0,
    AVS3A_SRC_NO_MEMORY = -1,
    AVS3A_SRC_INVALID_CONFIG = -2,
    AVS3A_SRC_INIT_FAILED = -3,
    AVS3A_SRC_DECODE_FAILED = -4,
    AVS3A_SRC_INVALID_STATE = -5,
};

/* codec_mode: 0 = mono, 1 = stereo, 2 = multichannel (channel-based only). */
avs3a_source_decoder* avs3a_source_create(void);
void avs3a_source_destroy(avs3a_source_decoder* decoder);

int avs3a_source_initialize(avs3a_source_decoder* decoder,
                            int sample_rate,
                            int channels,
                            int samples_per_channel,
                            int bitrate,
                            int codec_mode,
                            int neural_type,
                            int channel_config,
                            int source_bits,
                            const char* model_path);

/* payload points at the compressed payload (frame bytes after the header).
 * out_capacity_samples is the number of int16 samples available in out.
 * On success *out_samples is set to channels * samples_per_channel. */
int avs3a_source_decode(avs3a_source_decoder* decoder,
                        const unsigned char* payload,
                        int payload_len,
                        short* out,
                        int out_capacity_samples,
                        int* out_samples);

/* Returns the expected interleaved int16 sample count, or 0 if not initialized. */
int avs3a_source_expected_samples(const avs3a_source_decoder* decoder);

#ifdef __cplusplus
}
#endif

#endif /* AVS3A_DECODER_ADAPTER_H */
