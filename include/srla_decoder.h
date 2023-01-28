#ifndef SRLA_DECODER_H_INCLUDED
#define SRLA_DECODER_H_INCLUDED

#include "srla.h"
#include "srla_stdint.h"

/* デコーダコンフィグ */
struct SRLADecoderConfig {
    uint32_t max_num_channels; /* 最大チャンネル数 */
    uint32_t max_num_parameters; /* 最大パラメータ数 */
    uint8_t check_checksum; /* チェックサムによるデータ破損検査を行うか？ 1:ON それ以外:OFF */
};

/* デコーダハンドル */
struct SRLADecoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダデコード */
SRLAApiResult SRLADecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct SRLAHeader *header);

/* デコーダハンドルの作成に必要なワークサイズの計算 */
int32_t SRLADecoder_CalculateWorkSize(const struct SRLADecoderConfig *condig);

/* デコーダハンドルの作成 */
struct SRLADecoder* SRLADecoder_Create(const struct SRLADecoderConfig *condig, void *work, int32_t work_size);

/* デコーダハンドルの破棄 */
void SRLADecoder_Destroy(struct SRLADecoder *decoder);

/* デコーダにヘッダをセット */
SRLAApiResult SRLADecoder_SetHeader(
        struct SRLADecoder *decoder, const struct SRLAHeader *header);

/* 単一データブロックデコード */
SRLAApiResult SRLADecoder_DecodeBlock(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples,
        uint32_t *decode_size, uint32_t *num_decode_samples);

/* ヘッダを含めて全ブロックデコード */
SRLAApiResult SRLADecoder_DecodeWhole(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SRLA_DECODER_H_INCLUDED */
