#include "avs3a_reference.h"
#include "avs3a_vendor_layout.h"
static const uint32_t rates[9]={192000,96000,48000,44100,32000,24000,22050,16000,8000};
static const uint32_t mono[16]={16000,32000,44000,56000,64000,72000,80000,96000,128000,144000,164000,192000,0,0,0,0};
static const uint32_t stereo[16]={24000,32000,48000,64000,80000,96000,128000,144000,192000,256000,320000,0,0,0,0,0};
static uint32_t bits(const uint8_t* p,unsigned start,unsigned n){
 uint32_t v=0;for(unsigned i=0;i<n;++i){unsigned b=start+i;v=(v<<1)|((p[b/8]>>(7-b%8))&1u);}return v;
}
uint16_t avs3a_crc16(const uint8_t* data,size_t n){
 uint32_t crc=0xffffu;
 for(size_t i=0;i<n;++i){
  /* Vendor shifts old state then injects byte into LOW bits. NOT CCITT-FALSE. */
  for(unsigned j=0;j<8;++j)crc=((crc<<1)^((crc&0x8000u)?0x1021u:0u))&0xffffu;
  crc^=data[i];
 }
 return (uint16_t)crc;
}
int avs3a_parse_header(const uint8_t* p,size_t n,Avs3aHeader* h){
 if(!h||(!p&&n))return AVS3A_INVALID;
 if(n<7)return AVS3A_NEED_INPUT;
 if(bits(p,0,12)!=4095u||bits(p,12,4)!=2u||bits(p,16,1)!=0u)return AVS3A_INVALID;
 uint32_t nn=bits(p,17,3),profile=bits(p,20,3),sr=bits(p,23,4);
 if(profile!=0u||nn>1u)return AVS3A_UNSUPPORTED;
 if(sr>=9u)return AVS3A_INVALID;
 uint32_t cfg=bits(p,35,7),precision=bits(p,42,2),br=bits(p,44,4);
 if(cfg>1u)return AVS3A_UNSUPPORTED;
 if(precision==3u)return AVS3A_INVALID;
 if(precision!=1u)return AVS3A_UNSUPPORTED;
 uint32_t bitrate=(cfg==0u?mono:stereo)[br];
 if(!bitrate)return AVS3A_INVALID;
 volatile float ratio=(float)bitrate/(float)rates[sr];
 volatile float total=ratio*1024.0f;
 uint32_t frame_bits=(uint32_t)total;
 if(frame_bits<=56u)return AVS3A_INVALID;
 uint32_t payload_bits=frame_bits-56u,payload_bytes=(payload_bits+7u)/8u;
 if(payload_bytes>12300u)return AVS3A_INVALID;
 h->sample_rate=rates[sr];h->bitrate=bitrate;h->channels=cfg+1u;h->neural_type=nn;
 h->channel_config=cfg;h->frame_samples=1024u;h->source_bits=16u;h->header_bytes=7u;
 h->payload_bits=payload_bits;h->payload_bytes=payload_bytes;h->frame_bytes=7u+payload_bytes;
 h->crc=(bits(p,27,8)<<8)|bits(p,48,8);
 return AVS3A_OK;
}
int avs3a_validate_frame(const uint8_t* p,size_t n,Avs3aHeader* h){
 int r=avs3a_parse_header(p,n,h);if(r)return r;
 if(n<h->frame_bytes)return AVS3A_NEED_INPUT;
 return avs3a_crc16(p+7,h->payload_bytes)==h->crc?AVS3A_OK:AVS3A_CRC_MISMATCH;
}
static void put16(uint8_t* p,size_t o,uint32_t v){p[o]=(uint8_t)v;p[o+1]=(uint8_t)(v>>8);}
static void put32(uint8_t* p,size_t o,uint32_t v){for(unsigned i=0;i<4;++i)p[o+i]=(uint8_t)(v>>(i*8));}
int avs3a_prepare_new_decoder(void* state,size_t n,const Avs3aHeader* h){
 if(!state||!h||n!=sizeof(Avs3aDecoderLayout)||h->channels<1||h->channels>2||
    h->channel_config+1!=h->channels||h->frame_samples!=1024||h->source_bits!=16||
    h->neural_type>1||h->payload_bytes>12300||h->header_bytes!=7)return AVS3A_INVALID;
 int rate_ok=0,br_ok=0;
 for(unsigned i=0;i<9;++i)if(rates[i]==h->sample_rate)rate_ok=1;
 for(unsigned i=0;i<16;++i)if((h->channel_config==0?mono:stereo)[i]==h->bitrate&&h->bitrate)br_ok=1;
 if(!rate_ok||!br_ok)return AVS3A_INVALID;
 volatile float ratio=(float)h->bitrate/(float)h->sample_rate;
 volatile float total=ratio*1024.0f;
 uint32_t expected_bits=(uint32_t)total-56u;
 if(h->payload_bits!=expected_bits||h->payload_bytes!=(expected_bits+7u)/8u||
    h->frame_bytes!=7u+h->payload_bytes||h->crc>65535u)return AVS3A_INVALID;
 /* GetAvailableBits feeds signed-16 StereoBitsAllocation / mono/MCR paths.
    Transport syntax can represent more; conservatively reject that SDK budget. */
 if(h->payload_bits>32767u)return AVS3A_UNSUPPORTED;
 uint8_t* d=(uint8_t*)state;
 put32(d,4,h->sample_rate);put16(d,8,16);
 put32(d,12,h->bitrate);put32(d,16,h->bitrate);put32(d,20,h->channel_config);
 put16(d,24,h->channels);put16(d,26,0);put32(d,28,0);put32(d,32,0);
 put16(d,36,0);put16(d,38,0);put16(d,40,0);put16(d,42,h->channel_config);
 put16(d,44,0);put16(d,46,0);put16(d,48,1024);
 put32(d,52,h->payload_bits);put32(d,56,h->neural_type);put32(d,60,1);
 /* first_frame: allocation zero, Init sets 1, Decode clears; never reset per packet. */
 return AVS3A_OK;
}
