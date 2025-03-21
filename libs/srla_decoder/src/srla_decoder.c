#include "srla_decoder.h"

#include <stdlib.h>
#include <string.h>
#include "srla_lpc_synthesize.h"
#include "srla_internal.h"
#include "srla_utility.h"
#include "srla_coder.h"
#include "byte_array.h"
#include "bit_stream.h"
#include "static_huffman.h"
#include "lpc.h"

/* 内部状態フラグ */
#define SRLADECODER_STATUS_FLAG_ALLOCED_BY_OWN  (1 << 0)  /* 領域を自己割当した */
#define SRLADECODER_STATUS_FLAG_SET_HEADER      (1 << 1)  /* ヘッダセット済み */
#define SRLADECODER_STATUS_FLAG_CHECKSUM_CHECK  (1 << 2)  /* チェックサムの検査を行う */

/* 内部状態フラグ操作マクロ */
#define SRLADECODER_SET_STATUS_FLAG(decoder, flag)    ((decoder->status_flags) |= (flag))
#define SRLADECODER_CLEAR_STATUS_FLAG(decoder, flag)  ((decoder->status_flags) &= ~(flag))
#define SRLADECODER_GET_STATUS_FLAG(decoder, flag)    ((decoder->status_flags) & (flag))

/* デコーダハンドル */
struct SRLADecoder {
    struct SRLAHeader header; /* ヘッダ */
    uint32_t max_num_channels; /* デコード可能な最大チャンネル数 */
    uint32_t max_num_parameters; /* 最大パラメータ数 */
    struct SRLAPreemphasisFilter **de_emphasis; /* デエンファシスフィルタ */
    int32_t **lpc_coef; /* 各チャンネルのLPC係数(int) */
    uint32_t *rshifts; /* 各チャンネルのLPC係数右シフト量 */
    uint32_t *coef_order; /* 各チャンネルのLPC係数次数 */
    int32_t **ltp_coef; /* 各チャンネルのLTP係数(int) */
    uint32_t *ltp_period; /* 各チャンネルのLTP周期 */
    const struct StaticHuffmanTree* param_tree; /* 係数のハフマン木 */
    const struct StaticHuffmanTree* sum_param_tree; /* 和を取った係数のハフマン木 */
    const struct SRLAParameterPreset *parameter_preset; /* パラメータプリセット */
    uint8_t status_flags; /* 内部状態フラグ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* 生データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeRawData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);
/* 圧縮データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeCompressData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);
/* 無音データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeSilentData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size);

/* ヘッダデコード */
SRLAApiResult SRLADecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct SRLAHeader *header)
{
    const uint8_t *data_pos;
    uint32_t u32buf;
    uint16_t u16buf;
    uint8_t  u8buf;
    struct SRLAHeader tmp_header;

    /* 引数チェック */
    if ((data == NULL) || (header == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* データサイズが足りない */
    if (data_size < SRLA_HEADER_SIZE) {
        return SRLA_APIRESULT_INSUFFICIENT_DATA;
    }

    /* 読み出し用ポインタ設定 */
    data_pos = data;

    /* シグネチャ */
    {
        uint8_t buf[4];
        ByteArray_GetUint8(data_pos, &buf[0]);
        ByteArray_GetUint8(data_pos, &buf[1]);
        ByteArray_GetUint8(data_pos, &buf[2]);
        ByteArray_GetUint8(data_pos, &buf[3]);
        if ((buf[0] != '1') || (buf[1] != '2')
                || (buf[2] != '4') || (buf[3] != '9')) {
            return SRLA_APIRESULT_INVALID_FORMAT;
        }
    }

    /* シグネチャ検査に通ったら、エラーを起こさずに読み切る */

    /* フォーマットバージョン */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.format_version = u32buf;
    /* エンコーダバージョン */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.codec_version = u32buf;
    /* チャンネル数 */
    ByteArray_GetUint16BE(data_pos, &u16buf);
    tmp_header.num_channels = u16buf;
    /* サンプル数 */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.num_samples = u32buf;
    /* サンプリングレート */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.sampling_rate = u32buf;
    /* サンプルあたりビット数 */
    ByteArray_GetUint16BE(data_pos, &u16buf);
    tmp_header.bits_per_sample = u16buf;
    /* ブロックあたり最大サンプル数 */
    ByteArray_GetUint32BE(data_pos, &u32buf);
    tmp_header.max_num_samples_per_block = u32buf;
    /* パラメータプリセット */
    ByteArray_GetUint8(data_pos, &u8buf);
    tmp_header.preset = u8buf;

    /* ヘッダサイズチェック */
    SRLA_ASSERT((data_pos - data) == SRLA_HEADER_SIZE);

    /* 成功終了 */
    (*header) = tmp_header;
    return SRLA_APIRESULT_OK;
}

/* ヘッダのフォーマットチェック */
static SRLAError SRLADecoder_CheckHeaderFormat(const struct SRLAHeader *header)
{
    /* 内部モジュールなのでNULLが渡されたら落とす */
    SRLA_ASSERT(header != NULL);

    /* フォーマットバージョン */
    /* 補足）今のところは不一致なら無条件でエラー */
    if (header->format_version != SRLA_FORMAT_VERSION) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* コーデックバージョン */
    /* 補足）今のところは不一致なら無条件でエラー */
    if (header->codec_version != SRLA_CODEC_VERSION) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* チャンネル数 */
    if (header->num_channels == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* サンプル数 */
    if (header->num_samples == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* サンプリングレート */
    if (header->sampling_rate == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* ビット深度 */
    if (header->bits_per_sample == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* ブロックあたり最大サンプル数 */
    if (header->max_num_samples_per_block == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    /* パラメータプリセット */
    if (header->preset >= SRLA_NUM_PARAMETER_PRESETS) {
        return SRLA_ERROR_INVALID_FORMAT;
    }

    return SRLA_ERROR_OK;
}

/* デコーダハンドルの作成に必要なワークサイズの計算 */
int32_t SRLADecoder_CalculateWorkSize(const struct SRLADecoderConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if (config->max_num_channels == 0) {
        return -1;
    }

    /* 構造体サイズ（+メモリアラインメント） */
    work_size = sizeof(struct SRLADecoder) + SRLA_MEMORY_ALIGNMENT;
    /* デエンファシスフィルタのサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* パラメータ領域 */
    /* LPC係数(int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_parameters);
    /* 各チャンネルのLPC係数右シフト量 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* 各チャンネルのLPC係数次数 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* LTP係数(int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, SRLA_LTP_ORDER);
    /* LTP周期 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);

    return work_size;
}

/* デコーダハンドル作成 */
struct SRLADecoder *SRLADecoder_Create(const struct SRLADecoderConfig *config, void *work, int32_t work_size)
{
    uint32_t ch, l;
    struct SRLADecoder *decoder;
    uint8_t *work_ptr;
    uint8_t tmp_alloc_by_own = 0;

    /* 領域自前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = SRLADecoder_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < SRLADecoder_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if (config->max_num_channels == 0) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* 構造体領域確保 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    decoder = (struct SRLADecoder *)work_ptr;
    work_ptr += sizeof(struct SRLADecoder);

    /* 構造体メンバセット */
    decoder->work = work;
    decoder->max_num_channels = config->max_num_channels;
    decoder->max_num_parameters = config->max_num_parameters;
    decoder->status_flags = 0;  /* 状態クリア */
    if (tmp_alloc_by_own == 1) {
        SRLADECODER_SET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_ALLOCED_BY_OWN);
    }
    if (config->check_checksum == 1) {
        SRLADECODER_SET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_CHECKSUM_CHECK);
    }

    /* デエンファシスフィルタの作成 */
    SRLA_ALLOCATE_2DIMARRAY(decoder->de_emphasis,
            work_ptr, struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);

    /* バッファ領域の確保 全てのポインタをアラインメント */
    /* LPC係数(int) */
    SRLA_ALLOCATE_2DIMARRAY(decoder->lpc_coef,
            work_ptr, int32_t, config->max_num_channels, config->max_num_parameters);
    /* 各チャンネルのLPC係数右シフト量 */
    work_ptr = (uint8_t*)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    decoder->rshifts = (uint32_t*)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* 各チャンネルのLPC係数次数 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    decoder->coef_order = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* LTP係数(int) */
    SRLA_ALLOCATE_2DIMARRAY(decoder->ltp_coef,
        work_ptr, int32_t, config->max_num_channels, SRLA_LTP_ORDER);
    /* LTP周期 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    decoder->ltp_period = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);

    /* バッファオーバーランチェック */
    /* 補足）既にメモリを破壊している可能性があるので、チェックに失敗したら落とす */
    SRLA_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* プリエンファシスフィルタ初期化 */
    for (ch = 0; ch < config->max_num_channels; ch++) {
        for (l = 0; l < SRLA_NUM_PREEMPHASIS_FILTERS; l++) {
            SRLAPreemphasisFilter_Initialize(&decoder->de_emphasis[ch][l]);
        }
    }

    /* ハフマン木作成 */
    decoder->param_tree = SRLA_GetParameterHuffmanTree();
    decoder->sum_param_tree = SRLA_GetSumParameterHuffmanTree();

    return decoder;
}

/* デコーダハンドルの破棄 */
void SRLADecoder_Destroy(struct SRLADecoder *decoder)
{
    if (decoder != NULL) {
        if (SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_ALLOCED_BY_OWN)) {
            free(decoder->work);
        }
    }
}

/* デコーダにヘッダをセット */
SRLAApiResult SRLADecoder_SetHeader(
        struct SRLADecoder *decoder, const struct SRLAHeader *header)
{
    /* 引数チェック */
    if ((decoder == NULL) || (header == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダの有効性チェック */
    if (SRLADecoder_CheckHeaderFormat(header) != SRLA_ERROR_OK) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }

    /* デコーダの容量を越えてないかチェック */
    if (decoder->max_num_channels < header->num_channels) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 最大レイヤー数/パラメータ数のチェック */
    {
        const struct SRLAParameterPreset* preset = &g_srla_parameter_preset[header->preset];
        if (decoder->max_num_parameters < preset->max_num_parameters) {
            return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
        }
    }

    /* エンコードプリセットを取得 */
    SRLA_ASSERT(header->preset < SRLA_NUM_PARAMETER_PRESETS);
    decoder->parameter_preset = &g_srla_parameter_preset[header->preset];

    /* ヘッダセット */
    decoder->header = (*header);
    SRLADECODER_SET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_SET_HEADER);

    return SRLA_APIRESULT_OK;
}

/* 生データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeRawData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch, smpl;
    const struct SRLAHeader *header;
    const uint8_t *read_ptr;

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(decoder != NULL);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(data_size > 0);
    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(num_decode_samples > 0);
    SRLA_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    SRLA_ASSERT(num_channels >= header->num_channels);

    /* データサイズチェック */
    if (data_size < (header->bits_per_sample * num_decode_samples * header->num_channels) / 8) {
        return SRLA_APIRESULT_INSUFFICIENT_DATA;
    }

    /* 生データをチャンネルインターリーブで取得 */
    read_ptr = data;
    switch (header->bits_per_sample) {
    case 8:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint8_t buf;
                ByteArray_GetUint8(read_ptr, &buf);
                buffer[ch][smpl] = SRLAUTILITY_UINT32_TO_SINT32(buf);
                SRLA_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    case 16:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint16_t buf;
                ByteArray_GetUint16BE(read_ptr, &buf);
                buffer[ch][smpl] = SRLAUTILITY_UINT32_TO_SINT32(buf);
                SRLA_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    case 24:
        for (smpl = 0; smpl < num_decode_samples; smpl++) {
            for (ch = 0; ch < header->num_channels; ch++) {
                uint32_t buf;
                ByteArray_GetUint24BE(read_ptr, &buf);
                buffer[ch][smpl] = SRLAUTILITY_UINT32_TO_SINT32(buf);
                SRLA_ASSERT((uint32_t)(read_ptr - data) <= data_size);
            }
        }
        break;
    default: SRLA_ASSERT(0);
    }

    /* 読み取りサイズ取得 */
    (*decode_size) = (uint32_t)(read_ptr - data);

    return SRLA_APIRESULT_OK;
}

/* 圧縮データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeCompressData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch;
    int32_t l;
    struct BitStream reader;
    const struct SRLAHeader *header;
    SRLAChannelProcessMethod ch_process_method;

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(decoder != NULL);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(data_size > 0);
    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(num_decode_samples > 0);
    SRLA_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    SRLA_ASSERT(num_channels >= header->num_channels);

    /* ビットリーダ作成 */
    BitReader_Open(&reader, (uint8_t *)data, data_size);

    /* マルチチャンネル処理法の取得 */
    BitReader_GetBits(&reader, (uint32_t *)&ch_process_method, 2);

    /* パラメータ復号 */
    /* プリエンファシス */
    for (ch = 0; ch < num_channels; ch++) {
        uint32_t uval;
        int32_t head;
        /* プリエンファシス初期前値（全て共通） */
        BitReader_GetBits(&reader, &uval, header->bits_per_sample + 1U);
        head = SRLAUTILITY_UINT32_TO_SINT32(uval);
        for (l = 0; l < SRLA_NUM_PREEMPHASIS_FILTERS; l++) {
            decoder->de_emphasis[ch][l].prev = head;
        }
        /* プリエンファシス係数 */
        for (l = 0; l < SRLA_NUM_PREEMPHASIS_FILTERS; l++) {
            BitReader_GetBits(&reader, &uval, SRLA_PREEMPHASIS_COEF_SHIFT + 1);
            decoder->de_emphasis[ch][l].coef = SRLAUTILITY_UINT32_TO_SINT32(uval);
        }
    }
    /* LPC係数次数/LPC係数右シフト量/LPC係数 */
    for (ch = 0; ch < num_channels; ch++) {
        uint32_t i, uval, use_sum_coef;
        /* LPC係数次数 */
        BitReader_GetBits(&reader, &decoder->coef_order[ch], SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH);
        /* 各レイヤーでのLPC係数右シフト量 */
        BitReader_GetBits(&reader, &decoder->rshifts[ch], SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH);
        /* LPC係数 */
        BitReader_GetBits(&reader, &use_sum_coef, 1);
        /* 和をとって符号化しているかで場合分け */
        if (!use_sum_coef) {
            for (i = 0; i < decoder->coef_order[ch]; i++) {
                uval = StaticHuffman_GetCode(decoder->param_tree, &reader);
                decoder->lpc_coef[ch][i] = SRLAUTILITY_UINT32_TO_SINT32(uval);
            }
        } else {
            uval = StaticHuffman_GetCode(decoder->param_tree, &reader);
            decoder->lpc_coef[ch][0] = SRLAUTILITY_UINT32_TO_SINT32(uval);
            for (i = 1; i < decoder->coef_order[ch]; i++) {
                uval = StaticHuffman_GetCode(decoder->sum_param_tree, &reader);
                decoder->lpc_coef[ch][i] = SRLAUTILITY_UINT32_TO_SINT32(uval);
                /* 差をとって元に戻す */
                decoder->lpc_coef[ch][i] -= decoder->lpc_coef[ch][i - 1];
            }
        }
    }
    /* LTPフラグ/LTP周期/LTP係数 */
    for (ch = 0; ch < num_channels; ch++) {
        uint32_t uval;
        /* LTPフラグ */
        BitReader_GetBits(&reader, &uval, 1);
        if (uval != 0) {
            uint32_t i;
            /* LTP周期 */
            BitReader_GetBits(&reader, &uval, SRLA_LTP_PERIOD_BITWIDTH);
            decoder->ltp_period[ch] = uval + SRLA_LTP_MIN_PERIOD;
            /* LTP係数 */
            for (i = 0; i < SRLA_LTP_ORDER; i++) {
                BitReader_GetBits(&reader, &uval, SRLA_LTP_COEFFICIENT_BITWIDTH);
                decoder->ltp_coef[ch][i] = SRLAUTILITY_UINT32_TO_SINT32(uval);
            }
        } else {
            decoder->ltp_period[ch] = 0;
        }
    }

    /* 残差復号 */
    for (ch = 0; ch < header->num_channels; ch++) {
        SRLACoder_Decode(&reader, buffer[ch], num_decode_samples);
    }

    /* バイト境界に揃える */
    BitStream_Flush(&reader);

    /* 読み出しサイズの取得 */
    BitStream_Tell(&reader, (int32_t *)decode_size);

    /* ビットライタ破棄 */
    BitStream_Close(&reader);

    /* チャンネル毎に合成処理 */
    for (ch = 0; ch < header->num_channels; ch++) {
        /* LPC合成 */
        SRLALPC_Synthesize(buffer[ch],
            num_decode_samples, decoder->lpc_coef[ch], decoder->coef_order[ch], decoder->rshifts[ch]);
        /* LTP合成 */
        SRLALTP_Synthesize(buffer[ch],
            num_decode_samples, decoder->ltp_coef[ch], SRLA_LTP_ORDER,
            decoder->ltp_period[ch], SRLA_LTP_COEFFICIENT_BITWIDTH - 1);
        /* デエンファシス */
        SRLAPreemphasisFilter_MultiStageDeemphasis(
            decoder->de_emphasis[ch], SRLA_NUM_PREEMPHASIS_FILTERS, buffer[ch], num_decode_samples);
    }

    /* マルチチャンネル処理 */
    switch (ch_process_method) {
    case SRLA_CH_PROCESS_METHOD_NONE:
        break;
    case SRLA_CH_PROCESS_METHOD_MS:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_MStoLRConversion(buffer, num_decode_samples);
        break;
    case SRLA_CH_PROCESS_METHOD_LS:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_LStoLRConversion(buffer, num_decode_samples);
        break;
    case SRLA_CH_PROCESS_METHOD_SR:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_SRtoLRConversion(buffer, num_decode_samples);
        break;
    default:
        SRLA_ASSERT(0);
    }

    /* 成功終了 */
    return SRLA_APIRESULT_OK;
}

/* 無音データブロックデコード */
static SRLAApiResult SRLADecoder_DecodeSilentData(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t num_channels, uint32_t num_decode_samples,
        uint32_t *decode_size)
{
    uint32_t ch;
    const struct SRLAHeader *header;

    SRLAUTILITY_UNUSED_ARGUMENT(data_size);

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(decoder != NULL);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(num_decode_samples > 0);
    SRLA_ASSERT(decode_size != NULL);

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* チャンネル数不足もアサートで落とす */
    SRLA_ASSERT(num_channels >= header->num_channels);

    /* 全て無音で埋める */
    for (ch = 0; ch < header->num_channels; ch++) {
        memset(buffer[ch], 0, sizeof(int32_t) * num_decode_samples);
    }

    (*decode_size) = 0;
    return SRLA_APIRESULT_OK;
}

/* 単一データブロックデコード */
SRLAApiResult SRLADecoder_DecodeBlock(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples,
        uint32_t *decode_size, uint32_t *num_decode_samples)
{
    uint8_t buf8;
    uint16_t buf16;
    uint32_t buf32;
    uint16_t num_block_samples;
    uint32_t block_header_size, block_data_size;
    SRLAApiResult ret;
    SRLABlockDataType block_type;
    const struct SRLAHeader *header;
    const uint8_t *read_ptr;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL)
            || (buffer == NULL) || (decode_size == NULL)
            || (num_decode_samples == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダがまだセットされていない */
    if (!SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_SET_HEADER)) {
        return SRLA_APIRESULT_PARAMETER_NOT_SET;
    }

    /* ヘッダ取得 */
    header = &(decoder->header);

    /* バッファチャンネル数チェック */
    if (buffer_num_channels < header->num_channels) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* ブロックヘッダデコード */
    read_ptr = data;

    /* 同期コード */
    ByteArray_GetUint16BE(read_ptr, &buf16);
    /* 同期コード不一致 */
    if (buf16 != SRLA_BLOCK_SYNC_CODE) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* ブロックサイズ */
    ByteArray_GetUint32BE(read_ptr, &buf32);
    SRLA_ASSERT(buf32 > 0);
    /* データサイズ不足 */
    if ((buf32 + 6) > data_size) {
        return SRLA_APIRESULT_INSUFFICIENT_DATA;
    }
    /* ブロックチェックサム */
    ByteArray_GetUint16BE(read_ptr, &buf16);
    /* チェックするならばチェックサム計算を行い取得値との一致を確認 */
    if (SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_CHECKSUM_CHECK)) {
        /* チェックサム自体の領域は外すために-2 */
        uint16_t checksum = SRLAUtility_CalculateFletcher16CheckSum(read_ptr, buf32 - 2);
        if (checksum != buf16) {
            return SRLA_APIRESULT_DETECT_DATA_CORRUPTION;
        }
    }
    /* ブロックデータタイプ */
    ByteArray_GetUint8(read_ptr, &buf8);
    block_type = (SRLABlockDataType)buf8;
    /* ブロックチャンネルあたりサンプル数 */
    ByteArray_GetUint16BE(read_ptr, &num_block_samples);
    if (num_block_samples > buffer_num_samples) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }
    /* ブロックヘッダサイズ */
    block_header_size = (uint32_t)(read_ptr - data);

    /* データ部のデコード */
    switch (block_type) {
    case SRLA_BLOCK_DATA_TYPE_RAWDATA:
        ret = SRLADecoder_DecodeRawData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    case SRLA_BLOCK_DATA_TYPE_COMPRESSDATA:
        ret = SRLADecoder_DecodeCompressData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    case SRLA_BLOCK_DATA_TYPE_SILENT:
        ret = SRLADecoder_DecodeSilentData(decoder,
                read_ptr, data_size - block_header_size, buffer, header->num_channels, num_block_samples, &block_data_size);
        break;
    default:
        return SRLA_APIRESULT_INVALID_FORMAT;
    }

    /* データデコードに失敗している */
    if (ret != SRLA_APIRESULT_OK) {
        return ret;
    }

    /* デコードサイズ */
    (*decode_size) = block_header_size + block_data_size;

    /* デコードサンプル数 */
    (*num_decode_samples) = num_block_samples;

    /* デコード成功 */
    return SRLA_APIRESULT_OK;
}

/* ヘッダを含めて全ブロックデコード */
SRLAApiResult SRLADecoder_DecodeWhole(
        struct SRLADecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int32_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples)
{
    SRLAApiResult ret;
    uint32_t progress, ch, read_offset, read_block_size, num_decode_samples;
    const uint8_t *read_pos;
    int32_t *buffer_ptr[SRLA_MAX_NUM_CHANNELS];
    struct SRLAHeader tmp_header;
    const struct SRLAHeader *header;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL) || (buffer == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダデコードとデコーダへのセット */
    if ((ret = SRLADecoder_DecodeHeader(data, data_size, &tmp_header))
            != SRLA_APIRESULT_OK) {
        return ret;
    }
    if ((ret = SRLADecoder_SetHeader(decoder, &tmp_header))
            != SRLA_APIRESULT_OK) {
        return ret;
    }
    header = &(decoder->header);

    /* バッファサイズチェック */
    if ((buffer_num_channels < header->num_channels)
            || (buffer_num_samples < header->num_samples)) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    progress = 0;
    read_offset = SRLA_HEADER_SIZE;
    read_pos = data + SRLA_HEADER_SIZE;
    while ((progress < header->num_samples) && (read_offset < data_size)) {
        /* サンプル書き出し位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            buffer_ptr[ch] = &buffer[ch][progress];
        }
        /* ブロックデコード */
        if ((ret = SRLADecoder_DecodeBlock(decoder,
                        read_pos, data_size - read_offset,
                        buffer_ptr, buffer_num_channels, buffer_num_samples - progress,
                        &read_block_size, &num_decode_samples)) != SRLA_APIRESULT_OK) {
            return ret;
        }
        /* 進捗更新 */
        read_pos    += read_block_size;
        read_offset += read_block_size;
        progress    += num_decode_samples;
        SRLA_ASSERT(progress <= buffer_num_samples);
        SRLA_ASSERT(read_offset <= data_size);
    }

    /* 成功終了 */
    return SRLA_APIRESULT_OK;
}
