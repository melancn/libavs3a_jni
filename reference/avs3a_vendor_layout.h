#ifndef AVS3A_VENDOR_LAYOUT_H
#define AVS3A_VENDOR_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
/* Reconstructed layout, NOT a vendor-issued header. Use byte/memcpy access in adapter. */
typedef struct {
 int16_t first_frame; uint8_t opaque_02[2];
 int32_t sample_rate;
 int16_t source_bits; uint8_t opaque_0a[2];
 int32_t total_bitrate,bitrate_copy,channel_config;
 int16_t channel_count,object_count;
 int32_t object_bitrate,bed_bitrate;
 int16_t mixed_content_type,mixed_content,lfe_flag,decoder_format;
 int16_t option_44,hoa_order,frame_samples; uint8_t opaque_32[2];
 int32_t payload_bits,neural_codec_type,model_type;
 void *base_model,*hyper_model,*bitstream,*hoa_decoder,*mc_decoder,*stereo_decoder,*mono_aux;
 void *cores[16];
 void *metadata,*model_file;
} Avs3aDecoderLayout;
typedef struct { uint8_t payload[12300]; int32_t next_bit; } Avs3aBitstreamLayout;
/* ABI call adapters: do not interpret Init/Decode register contents as status. */
typedef void* (*Avs3aAllocFn)(void);
typedef void (*Avs3aInitFn)(void*, const char*);
typedef void (*Avs3aDecodeFn)(void*, int16_t*);
typedef void (*Avs3aDestroyFn)(void*);
typedef void (*Avs3aResetFn)(void*);
#if defined(__cplusplus)
#define AVS3A_ASSERT(c) static_assert((c), #c)
#else
#define AVS3A_ASSERT(c) _Static_assert((c), #c)
#endif
#define AVS3A_OFFSET(f,o) AVS3A_ASSERT(offsetof(Avs3aDecoderLayout,f)==(o))
AVS3A_OFFSET(first_frame,0); AVS3A_OFFSET(sample_rate,4); AVS3A_OFFSET(source_bits,8);
AVS3A_OFFSET(total_bitrate,12); AVS3A_OFFSET(bitrate_copy,16); AVS3A_OFFSET(channel_config,20);
AVS3A_OFFSET(channel_count,24); AVS3A_OFFSET(object_count,26); AVS3A_OFFSET(object_bitrate,28);
AVS3A_OFFSET(bed_bitrate,32); AVS3A_OFFSET(mixed_content_type,36); AVS3A_OFFSET(mixed_content,38);
AVS3A_OFFSET(lfe_flag,40); AVS3A_OFFSET(decoder_format,42); AVS3A_OFFSET(option_44,44);
AVS3A_OFFSET(hoa_order,46); AVS3A_OFFSET(frame_samples,48); AVS3A_OFFSET(payload_bits,52);
AVS3A_OFFSET(neural_codec_type,56); AVS3A_OFFSET(model_type,60); AVS3A_OFFSET(base_model,64);
AVS3A_OFFSET(hyper_model,64+sizeof(void*)); AVS3A_OFFSET(bitstream,64+2*sizeof(void*));
AVS3A_OFFSET(hoa_decoder,64+3*sizeof(void*)); AVS3A_OFFSET(mc_decoder,64+4*sizeof(void*));
AVS3A_OFFSET(stereo_decoder,64+5*sizeof(void*)); AVS3A_OFFSET(mono_aux,64+6*sizeof(void*));
AVS3A_OFFSET(cores,64+7*sizeof(void*)); AVS3A_OFFSET(metadata,64+23*sizeof(void*));
AVS3A_OFFSET(model_file,64+24*sizeof(void*));
AVS3A_ASSERT(sizeof(Avs3aDecoderLayout)==64+25*sizeof(void*));
AVS3A_ASSERT(sizeof(Avs3aDecoderLayout)==(sizeof(void*)==8?264:164));
AVS3A_ASSERT(offsetof(Avs3aBitstreamLayout,next_bit)==12300);
AVS3A_ASSERT(sizeof(Avs3aBitstreamLayout)==12304);
#undef AVS3A_OFFSET
#undef AVS3A_ASSERT
#endif
