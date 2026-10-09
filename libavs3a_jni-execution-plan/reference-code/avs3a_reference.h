#ifndef AVS3A_REFERENCE_H
#define AVS3A_REFERENCE_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
 uint32_t sample_rate,bitrate,channels,neural_type,channel_config;
 uint32_t frame_samples,source_bits,header_bytes,payload_bits,payload_bytes,frame_bytes,crc;
} Avs3aHeader;
enum { AVS3A_OK=0, AVS3A_NEED_INPUT=1, AVS3A_INVALID=-1, AVS3A_UNSUPPORTED=-2, AVS3A_CRC_MISMATCH=-3 };
/* Function declarations have C linkage when used by a C++ JNI adapter. */
#ifdef __cplusplus
extern "C" {
#endif
int avs3a_parse_header(const uint8_t*,size_t,Avs3aHeader*);
int avs3a_validate_frame(const uint8_t*,size_t,Avs3aHeader*);
uint16_t avs3a_crc16(const uint8_t*,size_t);
/* Only on fresh Avs3AllocDecoder memory, before Init. Pointers/cursor untouched. */
int avs3a_prepare_new_decoder(void*,size_t,const Avs3aHeader*);
#ifdef __cplusplus
}
#endif
#endif
