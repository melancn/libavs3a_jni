/* Static, bounded adapter around the vendored av3adecoder library.
 *
 * The vendored source is compiled into the same libavs3a_jni.so; there is no
 * dlopen and no private structure-offset access from outside this file.
 */
#include "decoder_adapter.h"

#include <stdlib.h>
#include <string.h>

#include "avs3_stat_dec.h"
#include "avs3_prot_dec.h"
#include "avs3_prot_com.h"
#include "avs3_cnst_com.h"
#include "avs3_options.h"

struct avs3a_source_decoder {
    AVS3DecoderHandle dec;
    int initialized;
    int expected_samples;
};

avs3a_source_decoder* avs3a_source_create(void) {
    avs3a_source_decoder* d =
        (avs3a_source_decoder*)calloc(1, sizeof(avs3a_source_decoder));
    if (!d) return NULL;
    d->dec = (AVS3DecoderHandle)calloc(1, sizeof(AVS3Decoder));
    if (!d->dec) {
        free(d);
        return NULL;
    }
    return d;
}

void avs3a_source_destroy(avs3a_source_decoder* d) {
    if (!d) return;
    if (d->dec) {
        /* Avs3DecoderDestroy frees the top-level AVS3Decoder allocation. */
        Avs3DecoderDestroy(d->dec);
        d->dec = NULL;
    }
    d->initialized = 0;
    d->expected_samples = 0;
    free(d);
}

int avs3a_source_initialize(avs3a_source_decoder* d,
                            int sample_rate,
                            int channels,
                            int samples_per_channel,
                            int bitrate,
                            int codec_mode,
                            int neural_type,
                            int channel_config,
                            int source_bits,
                            const char* model_path) {
    AVS3DecoderHandle h;
    int rc;

    if (!d || !d->dec) return AVS3A_SRC_INVALID_STATE;
    if (d->initialized) return AVS3A_SRC_INVALID_STATE;
    if (sample_rate <= 0 || channels <= 0 || samples_per_channel <= 0 || bitrate <= 0)
        return AVS3A_SRC_INVALID_CONFIG;
    if (codec_mode < 0 || codec_mode > 2) return AVS3A_SRC_INVALID_CONFIG;
    if (neural_type != NN_TYPE_DEFAULT_MAIN && neural_type != NN_TYPE_DEFAULT_LC)
        return AVS3A_SRC_INVALID_CONFIG;
    if (channels > MAX_CHANNELS) return AVS3A_SRC_INVALID_CONFIG;
    if (source_bits != 16) return AVS3A_SRC_INVALID_CONFIG;

    h = d->dec;

    h->outputFs = sample_rate;
    h->frameLength = (short)samples_per_channel;
    h->bitDepth = (short)source_bits;
    h->nnTypeConfig = (NnTypeConfig)neural_type;
    h->isMixedContent = 0;
    h->channelNumConfig = (ChannelNumConfig)channel_config;
    h->numChansOutput = (short)channels;
    h->numObjsOutput = 0;
    h->soundBedType = 0;
    h->bitrateBedMc = 0;
    h->bitratePerObj = 0;
    h->avs3CodecCore = AVS3_MDCT_CORE;
    h->modelType = HYPER;
    h->initFrame = 1;
    h->bInited = 0;
    h->totalBitrate = bitrate;
    h->hasLfe = (codec_mode == 2 && channel_config == CHANNEL_CONFIG_MC_4_0) ? 0 : 1;

    if (codec_mode == 0)
        h->avs3CodecFormat = AVS3_MONO_FORMAT;
    else if (codec_mode == 1)
        h->avs3CodecFormat = AVS3_STEREO_FORMAT;
    else
        h->avs3CodecFormat = AVS3_MC_FORMAT;

    rc = Avs3InitDecoder(h, &h->fModel, model_path);
    if (rc != 0 || h->hBitstream == NULL) return AVS3A_SRC_INIT_FAILED;
    h->bInited = 1;

    d->expected_samples = channels * samples_per_channel;
    d->initialized = 1;
    return AVS3A_SRC_OK;
}

int avs3a_source_decode(avs3a_source_decoder* d,
                        const unsigned char* payload,
                        int payload_len,
                        short* out,
                        int out_capacity_samples,
                        int* out_samples) {
    AVS3DecoderHandle h;
    int payload_bytes;
    int need_samples;

    if (!d || !d->dec || !d->initialized) return AVS3A_SRC_INVALID_STATE;
    if (!payload || payload_len <= 0 || !out || !out_samples)
        return AVS3A_SRC_DECODE_FAILED;

    h = d->dec;
    if (h->bitsPerFrame <= 0) return AVS3A_SRC_DECODE_FAILED;

    payload_bytes = (h->bitsPerFrame + 7) / 8;
    if (payload_bytes <= 0 || payload_bytes > MAX_BS_BYTES || payload_len < payload_bytes)
        return AVS3A_SRC_DECODE_FAILED;

    need_samples = d->expected_samples;
    if (need_samples <= 0 || out_capacity_samples < need_samples)
        return AVS3A_SRC_DECODE_FAILED;

    memcpy(h->hBitstream->bitstream, payload, (size_t)payload_bytes);
    h->hBitstream->nextBitPos = 0;

    Avs3Decode(h, out);
    ResetBitstream(h->hBitstream);

    *out_samples = need_samples;
    return AVS3A_SRC_OK;
}

int avs3a_source_expected_samples(const avs3a_source_decoder* d) {
    return d ? d->expected_samples : 0;
}
