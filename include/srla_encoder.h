#ifndef SRLA_ENCODER_H_INCLUDED
#define SRLA_ENCODER_H_INCLUDED

#include "srla.h"
#include "srla_stdint.h"

/* エンコードパラメータ */
struct SRLAEncodeParameter {
    uint16_t num_channels; /* 入力波形のチャンネル数 */
    uint16_t bits_per_sample; /* 入力波形のサンプルあたりビット数 */
    uint32_t sampling_rate; /* 入力波形のサンプリングレート */
    uint32_t min_num_samples_per_block; /* ブロックあたり最小サンプル数 */
    uint32_t max_num_samples_per_block; /* ブロックあたり最大サンプル数 */
    uint32_t num_lookahead_samples; /* 先読みサンプル数 */
    uint8_t preset; /* エンコードパラメータプリセット */
};

/* エンコーダコンフィグ */
struct SRLAEncoderConfig {
    uint32_t max_num_channels; /* 最大チャンネル数 */
    uint32_t min_num_samples_per_block; /* ブロックあたりサンプル数の下限値 */
    uint32_t max_num_samples_per_block; /* ブロックあたりサンプル数の上限値 */
    uint32_t max_num_lookahead_samples; /* 最大先読みサンプル数 */
    uint32_t max_num_parameters; /* 最大のパラメータ数 */
};

/* エンコーダハンドル */
struct SRLAEncoder;

/* ブロックエンコードコールバック */
typedef void (*SRLAEncoder_EncodeBlockCallback)(
    uint32_t num_samples, uint32_t progress_samples, const uint8_t* encoded_block_data, uint32_t block_data_size);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダエンコード */
SRLAApiResult SRLAEncoder_EncodeHeader(
    const struct SRLAHeader *header, uint8_t *data, uint32_t data_size);

/* エンコーダハンドル作成に必要なワークサイズ計算 */
int32_t SRLAEncoder_CalculateWorkSize(const struct SRLAEncoderConfig *config);

/* エンコーダハンドル作成 */
struct SRLAEncoder *SRLAEncoder_Create(const struct SRLAEncoderConfig *config, void *work, int32_t work_size);

/* エンコーダハンドルの破棄 */
void SRLAEncoder_Destroy(struct SRLAEncoder *encoder);

/* エンコードパラメータの設定 */
SRLAApiResult SRLAEncoder_SetEncodeParameter(
    struct SRLAEncoder *encoder, const struct SRLAEncodeParameter *parameter);

/* 単一データブロックサイズ計算 */
SRLAApiResult SRLAEncoder_ComputeBlockSize(
    struct SRLAEncoder *encoder, const int32_t *const *input, uint32_t num_samples,
    uint32_t *output_size);

/* 単一データブロックエンコード */
SRLAApiResult SRLAEncoder_EncodeBlock(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* 最適なブロック分割探索を含めたエンコード */
SRLAApiResult SRLAEncoder_EncodeOptimalPartitionedBlock(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* ヘッダ含めファイル全体をエンコード */
SRLAApiResult SRLAEncoder_EncodeWhole(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size,
    SRLAEncoder_EncodeBlockCallback encode_callback);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SRLA_ENCODER_H_INCLUDED */
