#include "srla_encoder.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <float.h>
#include "srla_lpc_predict.h"
#include "srla_internal.h"
#include "srla_utility.h"
#include "byte_array.h"
#include "bit_stream.h"
#include "lpc.h"
#include "srla_coder.h"

/* エンコーダハンドル */
struct SRLAEncoder {
    struct SRLAHeader header; /* ヘッダ */
    struct SRLACoder *coder; /* 符号化ハンドル */
    uint32_t max_num_channels; /* バッファチャンネル数 */
    uint32_t max_num_samples_per_block; /* バッファサンプル数 */
    uint32_t max_num_parameters; /* 最大パラメータ数 */
    uint8_t set_parameter; /* パラメータセット済み？ */
    struct LPCCalculator *lpcc; /* LPC計算ハンドル */
    struct SRLAPreemphasisFilter **pre_emphasis; /* プリエンファシスフィルタ */
    int32_t **pre_emphasis_prev; /* プリエンファシスフィルタの直前のサンプル */
    double **params_double; /* 各チャンネルのLPC係数(double) */
    int32_t **params_int; /* 各チャンネルのLPC係数(int) */
    uint32_t *rshifts; /* 各チャンネルのLPC係数右シフト量 */
    uint32_t *coef_order; /* 各チャンネルのLPC係数次数 */
    int32_t **buffer_int; /* 信号バッファ(int) */
    int32_t **residual; /* 残差信号 */
    double *buffer_double; /* 信号バッファ(double) */
    const struct SRLAParameterPreset *parameter_preset; /* パラメータプリセット */
    uint8_t alloced_by_own; /* 領域を自前確保しているか？ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* エンコードパラメータをヘッダに変換 */
static SRLAError SRLAEncoder_ConvertParameterToHeader(
        const struct SRLAEncodeParameter *parameter, uint32_t num_samples,
        struct SRLAHeader *header);
/* ブロックデータタイプの判定 */
static SRLABlockDataType SRLAEncoder_DecideBlockDataType(
        struct SRLAEncoder *encoder, const int32_t *const *input, uint32_t num_samples);

/* ヘッダエンコード */
SRLAApiResult SRLAEncoder_EncodeHeader(
        const struct SRLAHeader *header, uint8_t *data, uint32_t data_size)
{
    uint8_t *data_pos;

    /* 引数チェック */
    if ((header == NULL) || (data == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* 出力先バッファサイズ不足 */
    if (data_size < SRLA_HEADER_SIZE) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* ヘッダ異常値のチェック */
    /* データに書き出す（副作用）前にできる限りのチェックを行う */
    /* チャンネル数 */
    if (header->num_channels == 0) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* サンプル数 */
    if (header->num_samples == 0) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* サンプリングレート */
    if (header->sampling_rate == 0) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* ビット深度 */
    if (header->bits_per_sample == 0) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* ブロックあたりサンプル数 */
    if (header->num_samples_per_block == 0) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }
    /* パラメータプリセット */
    if (header->preset >= SRLA_NUM_PARAMETER_PRESETS) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }

    /* 書き出し用ポインタ設定 */
    data_pos = data;

    /* シグネチャ */
    ByteArray_PutUint8(data_pos, '1');
    ByteArray_PutUint8(data_pos, '2');
    ByteArray_PutUint8(data_pos, '4');
    ByteArray_PutUint8(data_pos, '9');
    /* フォーマットバージョン
    * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
    ByteArray_PutUint32BE(data_pos, SRLA_FORMAT_VERSION);
    /* コーデックバージョン
    * 補足）ヘッダの設定値は無視してマクロ値を書き込む */
    ByteArray_PutUint32BE(data_pos, SRLA_CODEC_VERSION);
    /* チャンネル数 */
    ByteArray_PutUint16BE(data_pos, header->num_channels);
    /* サンプル数 */
    ByteArray_PutUint32BE(data_pos, header->num_samples);
    /* サンプリングレート */
    ByteArray_PutUint32BE(data_pos, header->sampling_rate);
    /* サンプルあたりビット数 */
    ByteArray_PutUint16BE(data_pos, header->bits_per_sample);
    /* ブロックあたりサンプル数 */
    ByteArray_PutUint32BE(data_pos, header->num_samples_per_block);
    /* パラメータプリセット */
    ByteArray_PutUint8(data_pos, header->preset);

    /* ヘッダサイズチェック */
    SRLA_ASSERT((data_pos - data) == SRLA_HEADER_SIZE);

    /* 成功終了 */
    return SRLA_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static SRLAError SRLAEncoder_ConvertParameterToHeader(
        const struct SRLAEncodeParameter *parameter, uint32_t num_samples,
        struct SRLAHeader *header)
{
    struct SRLAHeader tmp_header = { 0, };

    /* 引数チェック */
    if ((parameter == NULL) || (header == NULL)) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    /* パラメータのチェック */
    if (parameter->num_channels == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    if (parameter->bits_per_sample == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    if (parameter->sampling_rate == 0) {
        return SRLA_ERROR_INVALID_FORMAT;
    }
    if (parameter->preset >= SRLA_NUM_PARAMETER_PRESETS) {
        return SRLA_ERROR_INVALID_FORMAT;
    }

    /* 総サンプル数 */
    tmp_header.num_samples = num_samples;

    /* 対応するメンバをコピー */
    tmp_header.num_channels = parameter->num_channels;
    tmp_header.sampling_rate = parameter->sampling_rate;
    tmp_header.bits_per_sample = parameter->bits_per_sample;
    tmp_header.preset = parameter->preset;
    tmp_header.num_samples_per_block = parameter->num_samples_per_block;

    /* 成功終了 */
    (*header) = tmp_header;
    return SRLA_ERROR_OK;
}

/* エンコーダハンドル作成に必要なワークサイズ計算 */
int32_t SRLAEncoder_CalculateWorkSize(const struct SRLAEncoderConfig *config)
{
    int32_t work_size, tmp_work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_num_samples_per_block == 0)
            || (config->max_num_channels == 0)
            || (config->max_num_parameters == 0)) {
        return -1;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters > config->max_num_samples_per_block) {
        return -1;
    }

    /* ハンドル本体のサイズ */
    work_size = sizeof(struct SRLAEncoder) + SRLA_MEMORY_ALIGNMENT;

    /* LPC計算ハンドルのサイズ */
    {
        struct LPCCalculatorConfig lpcc_config;
        lpcc_config.max_num_samples = config->max_num_samples_per_block;
        lpcc_config.max_order = config->max_num_parameters;
        if ((tmp_work_size = LPCCalculator_CalculateWorkSize(&lpcc_config)) < 0) {
            return -1;
        }
        work_size += tmp_work_size;
    }

    /* 符号化ハンドルのサイズ */
    if ((tmp_work_size = SRLACoder_CalculateWorkSize()) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* プリエンファシスフィルタのサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* パラメータバッファ領域 */
    /* LPC係数(int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_parameters);
    /* LPC係数(double) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(double, config->max_num_channels, config->max_num_parameters);
    /* 各チャンネルのLPC係数右シフト量 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* 各チャンネルのLPC係数次数 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* 信号処理バッファのサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block);
    work_size += (int32_t)(config->max_num_samples_per_block * sizeof(double) + SRLA_MEMORY_ALIGNMENT);
    /* 残差信号のサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block);

    return work_size;
}

/* エンコーダハンドル作成 */
struct SRLAEncoder* SRLAEncoder_Create(const struct SRLAEncoderConfig* config, void* work, int32_t work_size)
{
    uint32_t ch, l;
    struct SRLAEncoder* encoder;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t* work_ptr;

    /* ワーク領域時前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = SRLAEncoder_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
        || (work_size < SRLAEncoder_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_num_channels == 0)
        || (config->max_num_samples_per_block == 0)
        || (config->max_num_parameters == 0)) {
        return NULL;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters > config->max_num_samples_per_block) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t*)work;

    /* エンコーダハンドル領域確保 */
    work_ptr = (uint8_t*)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder = (struct SRLAEncoder*)work_ptr;
    work_ptr += sizeof(struct SRLAEncoder);

    /* エンコーダメンバ設定 */
    encoder->set_parameter = 0;
    encoder->alloced_by_own = tmp_alloc_by_own;
    encoder->work = work;
    encoder->max_num_channels = config->max_num_channels;
    encoder->max_num_samples_per_block = config->max_num_samples_per_block;
    encoder->max_num_parameters = config->max_num_parameters;

    /* LPC計算ハンドルの作成 */
    {
        int32_t lpcc_size;
        struct LPCCalculatorConfig lpcc_config;
        lpcc_config.max_num_samples = config->max_num_samples_per_block;
        lpcc_config.max_order = config->max_num_parameters;
        lpcc_size = LPCCalculator_CalculateWorkSize(&lpcc_config);
        if ((encoder->lpcc = LPCCalculator_Create(&lpcc_config, work_ptr, lpcc_size)) == NULL) {
            return NULL;
        }
        work_ptr += lpcc_size;
    }

    /* 符号化ハンドルの作成 */
    {
        const int32_t coder_size = SRLACoder_CalculateWorkSize();
        if ((encoder->coder = SRLACoder_Create(work_ptr, coder_size)) == NULL) {
            return NULL;
        }
        work_ptr += coder_size;
    }

    /* プリエンファシスフィルタの作成 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->pre_emphasis,
            work_ptr, struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* プリエンファシスフィルタのバッファ領域 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->pre_emphasis_prev,
            work_ptr, int32_t, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);

    /* バッファ領域の確保 全てのポインタをアラインメント */
    /* LPC係数(int) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->params_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_parameters);
    /* LPC係数(double) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->params_double,
            work_ptr, double, config->max_num_channels, config->max_num_parameters);
    /* 各層のLPC係数右シフト量 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->rshifts = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* 各層のLPC係数次数 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->coef_order = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);

    /* 信号処理用バッファ領域 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->buffer_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);
    SRLA_ALLOCATE_2DIMARRAY(encoder->residual,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);

    /* doubleバッファ */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->buffer_double = (double *)work_ptr;
    work_ptr += config->max_num_samples_per_block * sizeof(double);

    /* バッファオーバーランチェック */
    /* 補足）既にメモリを破壊している可能性があるので、チェックに失敗したら落とす */
    SRLA_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* プリエンファシスフィルタ初期化 */
    for (ch = 0; ch < config->max_num_channels; ch++) {
        for (l = 0; l < SRLA_NUM_PREEMPHASIS_FILTERS; l++) {
            SRLAPreemphasisFilter_Initialize(&encoder->pre_emphasis[ch][l]);
        }
    }

    return encoder;
}

/* エンコーダハンドルの破棄 */
void SRLAEncoder_Destroy(struct SRLAEncoder *encoder)
{
    if (encoder != NULL) {
        SRLACoder_Destroy(encoder->coder);
        if (encoder->alloced_by_own == 1) {
            free(encoder->work);
        }
    }
}

/* エンコードパラメータの設定 */
SRLAApiResult SRLAEncoder_SetEncodeParameter(
        struct SRLAEncoder *encoder, const struct SRLAEncodeParameter *parameter)
{
    struct SRLAHeader tmp_header;

    /* 引数チェック */
    if ((encoder == NULL) || (parameter == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータ設定がおかしくないか、ヘッダへの変換を通じて確認 */
    /* 総サンプル数はダミー値を入れる */
    if (SRLAEncoder_ConvertParameterToHeader(parameter, 0, &tmp_header) != SRLA_ERROR_OK) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }

    /* エンコーダの容量を越えてないかチェック */
    if ((encoder->max_num_samples_per_block < parameter->num_samples_per_block)
        || (encoder->max_num_channels < parameter->num_channels)) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }
    /* ブロックあたりサンプル数のセット */
    tmp_header.num_samples_per_block = parameter->num_samples_per_block;

    /* ヘッダ設定 */
    encoder->header = tmp_header;

    /* エンコードプリセットを取得 */
    SRLA_ASSERT(parameter->preset < SRLA_NUM_PARAMETER_PRESETS);
    encoder->parameter_preset = &g_srla_parameter_preset[parameter->preset];

    /* パラメータ設定済みフラグを立てる */
    encoder->set_parameter = 1;

    return SRLA_APIRESULT_OK;
}

/* ブロックデータタイプの判定 */
static SRLABlockDataType SRLAEncoder_DecideBlockDataType(
        struct SRLAEncoder *encoder, const int32_t *const *input, uint32_t num_samples)
{
    uint32_t ch, smpl;
    double mean_length;
    const struct SRLAHeader *header;

    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(encoder->set_parameter == 1);

    header = &encoder->header;

    /* 平均符号長の計算 */
    mean_length = 0.0;
    for (ch = 0; ch < header->num_channels; ch++) {
        double len;
        LPCApiResult ret;
        /* 入力をdouble化 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            encoder->buffer_double[smpl] = input[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        }
        /* 推定符号長計算 */
        ret = LPCCalculator_EstimateCodeLength(encoder->lpcc,
            encoder->buffer_double, num_samples,
            header->bits_per_sample, encoder->parameter_preset->max_num_parameters, &len, LPC_WINDOWTYPE_RECTANGULAR);
        SRLA_ASSERT(ret == LPC_APIRESULT_OK);
        mean_length += len;
    }
    mean_length /= header->num_channels;

    /* ビット幅に占める比に変換 */
    mean_length /= header->bits_per_sample;

    /* データタイプ判定 */

    /* 圧縮が効きにくい: 生データ出力 */
    if (mean_length >= SRLA_ESTIMATED_CODELENGTH_THRESHOLD) {
        return SRLA_BLOCK_DATA_TYPE_RAWDATA;
    }

    /* 無音判定 */
    for (ch = 0; ch < header->num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            if (input[ch][smpl] != 0) {
                goto NOT_SILENCE;
            }
        }
    }
    return SRLA_BLOCK_DATA_TYPE_SILENT;

NOT_SILENCE:
    /* それ以外は圧縮データ */
    return SRLA_BLOCK_DATA_TYPE_COMPRESSDATA;
}

/* 生データブロックエンコード */
static SRLAApiResult SRLAEncoder_EncodeRawData(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t ch, smpl;
    const struct SRLAHeader *header;
    uint8_t *data_ptr;

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(num_samples > 0);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(data_size > 0);
    SRLA_ASSERT(output_size != NULL);

    header = &(encoder->header);

    /* 書き込み先のバッファサイズチェック */
    if (data_size < (header->bits_per_sample * num_samples * header->num_channels) / 8) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 生データをチャンネルインターリーブして出力 */
    data_ptr = data;
    switch (header->bits_per_sample) {
            case 8:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint8(data_ptr, SRLAUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        SRLA_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            case 16:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint16BE(data_ptr, SRLAUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        SRLA_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            case 24:
                for (smpl = 0; smpl < num_samples; smpl++) {
                    for (ch = 0; ch < header->num_channels; ch++) {
                        ByteArray_PutUint24BE(data_ptr, SRLAUTILITY_SINT32_TO_UINT32(input[ch][smpl]));
                        SRLA_ASSERT((uint32_t)(data_ptr - data) < data_size);
                    }
                }
                break;
            default:
                SRLA_ASSERT(0);
    }

    /* 書き込みサイズ取得 */
    (*output_size) = (uint32_t)(data_ptr - data);

    return SRLA_APIRESULT_OK;
}

/* Recursive Golomb-Rice符号の平均符号長 */
static double SRLAEncoder_CalculateRGRMeanCodeLength(double mean_abs_error, uint32_t bps)
{
    const double intmean = mean_abs_error * (1 << bps); /* 整数量子化した時の平均値 */
    const double rho = 1.0 / (1.0 + intmean);
    const uint32_t k2 = (uint32_t)SRLAUTILITY_MAX(0, SRLAUtility_Log2(log(0.5127629514) / log(1.0 - rho)));
    const uint32_t k1 = k2 + 1;
    const double k1factor = pow(1.0 - rho, (double)(1 << k1));
    const double k2factor = pow(1.0 - rho, (double)(1 << k2));
    return (1.0 + k1) * (1.0 - k1factor) + (1.0 + k2 + (1.0 / (1.0 - k2factor))) * k1factor;
}

/* 最適なLPC次数の選択 */
static SRLAError SRLAEncoder_SelectBestLPCOrder(struct SRLAEncoder *encoder,
    const double *input, uint32_t num_samples, SRLAChannelLPCOrderDecisionTactics tactics,
    uint32_t max_coef_order, uint32_t *best_coef_order)
{
    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(input == encoder->buffer_double); /* 現状エンコーダハンドルの領域の使用を想定 */
    SRLA_ASSERT(best_coef_order != NULL);

    switch (tactics) {
    case SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED:
        /* 最大次数を常に選択 */
        (*best_coef_order) = max_coef_order;
        return SRLA_ERROR_OK;
    case SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_SEARCH:
        /* 網羅探索 */
    {
        LPCApiResult ret;
        double minlen, len, mabse;
        uint32_t i, order, smpl, tmp_best_order = 0;
        double coefs[SRLA_MAX_COEFFICIENT_ORDER][SRLA_MAX_COEFFICIENT_ORDER];
        double *pcoefs[SRLA_MAX_COEFFICIENT_ORDER];
        for (i = 0; i < SRLA_MAX_COEFFICIENT_ORDER; i++) {
            pcoefs[i] = &coefs[i][0];
        }
        /* 次数選択のため係数計算 */
        ret = LPCCalculator_CalculateMultipleLPCCoefficients(encoder->lpcc,
            input, num_samples, pcoefs, max_coef_order, LPC_WINDOWTYPE_WELCH, 1e-6);
        SRLA_ASSERT(ret == LPC_APIRESULT_OK);

        minlen = FLT_MAX;
        for (order = 1; order <= max_coef_order; order++) {
            const double *coef = pcoefs[order - 1];
            mabse = 0.0;
            for (smpl = order; smpl < num_samples; smpl++) {
                double residual = input[smpl];
                for (i = 0; i < order; i++) {
                    residual += coef[i] * input[smpl - i - 1];
                }
                mabse += SRLAUTILITY_ABS(residual);
            }
            /* 残差符号のサイズ */
            len = SRLAEncoder_CalculateRGRMeanCodeLength(mabse / num_samples, encoder->header.bits_per_sample) * num_samples;
            /* 係数のサイズ */
            len += SRLA_LPC_COEFFICIENT_BITWIDTH * order;
            if (minlen > len) {
                minlen = len;
                tmp_best_order = order;
            }
        }
        /* 結果を設定 */
        SRLA_ASSERT(tmp_best_order != 0);
        (*best_coef_order) = tmp_best_order;
        return SRLA_ERROR_OK;
    }
    case SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION:
        /* 残差分散の推測による網羅探索 */
    {
        LPCApiResult ret;
        double minlen, len, mabse;
        uint32_t order, tmp_best_order = 0;
        double error_vars[SRLA_MAX_COEFFICIENT_ORDER + 1];

        /* 残差分散の計算 */
        ret = LPCCalculator_CalculateErrorVariances(encoder->lpcc,
            input, num_samples, error_vars, max_coef_order, LPC_WINDOWTYPE_WELCH, 1e-6);
        SRLA_ASSERT(ret == LPC_APIRESULT_OK);

        minlen = FLT_MAX;
        for (order = 1; order <= max_coef_order; order++) {
            /* Laplace分布の仮定で残差分散から平均絶対値を推定 */
            mabse = sqrt(error_vars[order] / 2.0);
            /* 残差符号のサイズ */
            len = SRLAEncoder_CalculateRGRMeanCodeLength(mabse, encoder->header.bits_per_sample) * num_samples;
            /* 係数のサイズ */
            len += SRLA_LPC_COEFFICIENT_BITWIDTH * order;
            if (minlen > len) {
                minlen = len;
                tmp_best_order = order;
            }
        }
        /* 結果を設定 */
        SRLA_ASSERT(tmp_best_order != 0);
        (*best_coef_order) = tmp_best_order;
        return SRLA_ERROR_OK;
    }
    default:
        SRLA_ASSERT(0);
    }

    return SRLA_ERROR_NG;
}

/* 圧縮データブロックエンコード */
static SRLAApiResult SRLAEncoder_EncodeCompressData(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t ch;
    struct BitStream writer;
    const struct SRLAHeader *header;
    SRLAChannelProcessMethod ch_process_method = SRLA_CH_PROCESS_METHOD_INVALID;

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(num_samples > 0);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(data_size > 0);
    SRLA_ASSERT(output_size != NULL);

    /* ヘッダ取得 */
    header = &(encoder->header);

    /* マルチチャンネル処理法の決定 */
    if (header->num_channels == 1) {
        ch_process_method = SRLA_CH_PROCESS_METHOD_NONE;
    } else {
        switch (encoder->parameter_preset->ch_process_method_tactics) {
        case SRLA_CH_PROCESS_METHOD_TACTICS_NONE:
            ch_process_method = SRLA_CH_PROCESS_METHOD_NONE;
            break;
        case SRLA_CH_PROCESS_METHOD_TACTICS_MS_FIXED:
            ch_process_method = SRLA_CH_PROCESS_METHOD_MS;
            break;
        case SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE:
            if (header->num_channels >= 2) {
                uint32_t smpl;
                double est_len[4];
                /* 入力をバッファにコピー */
                for (ch = 0; ch < 2; ch++) {
                    memcpy(encoder->buffer_int[ch], input[ch], sizeof(int32_t) * num_samples);
                }
                /* MS信号の生成 */
                SRLAUtility_LRtoMSConversion(encoder->buffer_int, num_samples);

                /* 各チャンネルの推定符号長を計算 */
                for (ch = 0; ch < 2; ch++) {
                    /* L,R */
                    for (smpl = 0; smpl < num_samples; smpl++) {
                        encoder->buffer_double[smpl] = input[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
                    }
                    LPCCalculator_EstimateCodeLength(encoder->lpcc,
                        encoder->buffer_double, num_samples, header->bits_per_sample, encoder->parameter_preset->max_num_parameters,
                        &est_len[ch], LPC_WINDOWTYPE_WELCH);
                    /* M,S */
                    for (smpl = 0; smpl < num_samples; smpl++) {
                        encoder->buffer_double[smpl] = encoder->buffer_int[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
                    }
                    LPCCalculator_EstimateCodeLength(encoder->lpcc,
                        encoder->buffer_double, num_samples, header->bits_per_sample, encoder->parameter_preset->max_num_parameters,
                        &est_len[2 + ch], LPC_WINDOWTYPE_WELCH);
                }
                /* 最小の符号長を選択 */
                {
                    uint32_t i;
                    SRLAChannelProcessMethod argmin;
                    double len[4], min;
                    SRLA_STATIC_ASSERT((SRLA_CH_PROCESS_METHOD_NONE == 0) && (SRLA_CH_PROCESS_METHOD_MS == 1)
                        && (SRLA_CH_PROCESS_METHOD_LS == 2) && (SRLA_CH_PROCESS_METHOD_RS == 3));
                    len[SRLA_CH_PROCESS_METHOD_NONE] = est_len[0] + est_len[1];
                    len[SRLA_CH_PROCESS_METHOD_MS] = est_len[2] + est_len[3];
                    len[SRLA_CH_PROCESS_METHOD_LS] = est_len[0] + est_len[3];
                    len[SRLA_CH_PROCESS_METHOD_RS] = est_len[1] + est_len[3];
                    min = len[SRLA_CH_PROCESS_METHOD_NONE]; argmin = SRLA_CH_PROCESS_METHOD_NONE;
                    for (i = 1; i < 4; i++) {
                        if (min > len[i]) {
                            min = len[i];
                            argmin = (SRLAChannelProcessMethod)i;
                        }
                    }
                    ch_process_method = argmin;
                }
            }
            break;
        default:
            SRLA_ASSERT(0);
        }
    }

    /* 入力をバッファにコピー */
    for (ch = 0; ch < header->num_channels; ch++) {
        memcpy(encoder->buffer_int[ch], input[ch], sizeof(int32_t) * num_samples);
        /* バッファサイズより小さい入力のときは、末尾を0埋め */
        if (num_samples < encoder->max_num_samples_per_block) {
            const uint32_t remain = encoder->max_num_samples_per_block - num_samples;
            memset(&encoder->buffer_int[ch][num_samples], 0, sizeof(int32_t) * remain);
        }
    }

    /* マルチチャンネル処理 */
    switch (ch_process_method) {
    case SRLA_CH_PROCESS_METHOD_NONE:
        break;
    case SRLA_CH_PROCESS_METHOD_MS:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_LRtoMSConversion(encoder->buffer_int, num_samples);
        break;
    case SRLA_CH_PROCESS_METHOD_LS:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_LRtoLSConversion(encoder->buffer_int, num_samples);
        break;
    case SRLA_CH_PROCESS_METHOD_RS:
        SRLA_ASSERT(header->num_channels >= 2);
        SRLAUtility_LRtoRSConversion(encoder->buffer_int, num_samples);
        break;
    default:
        SRLA_ASSERT(0);
    }

    /* プリエンファシス */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t p;
        for (p = 0; p < SRLA_NUM_PREEMPHASIS_FILTERS; p++) {
            /* 直前値には先頭の同一値が続くと考える */
            encoder->pre_emphasis[ch][p].prev = encoder->pre_emphasis_prev[ch][p] = encoder->buffer_int[ch][0];
            SRLAPreemphasisFilter_CalculateCoefficient(&encoder->pre_emphasis[ch][p], encoder->buffer_int[ch], num_samples);
            SRLAPreemphasisFilter_Preemphasis(&encoder->pre_emphasis[ch][p], encoder->buffer_int[ch], num_samples);
        }
    }

    /* チャンネル毎にパラメータ計算 */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t smpl, p;
        LPCApiResult ret;
        /* double精度の信号に変換（[-1,1]の範囲に正規化） */
        for (smpl = 0; smpl < num_samples; smpl++) {
            encoder->buffer_double[smpl] = encoder->buffer_int[ch][smpl] * pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        }
        /* 次数選択 */
        SRLAEncoder_SelectBestLPCOrder(encoder,
            encoder->buffer_double, num_samples, encoder->parameter_preset->lpc_order_tactics,
            encoder->parameter_preset->max_num_parameters, &encoder->coef_order[ch]);
        /* LPC係数計算 */
        ret = LPCCalculator_CalculateLPCCoefficientsSVR(encoder->lpcc,
            encoder->buffer_double, num_samples,
            encoder->params_double[ch], encoder->coef_order[ch], encoder->parameter_preset->svr_max_num_iterations,
            LPC_WINDOWTYPE_WELCH, 1e-6, encoder->parameter_preset->margin_list, encoder->parameter_preset->margin_list_size);
        SRLA_ASSERT(ret == LPC_APIRESULT_OK);
        /* 畳み込み演算でインデックスが増える方向にしたい都合上パラメータ順序を変転 */
        for (p = 0; p < encoder->coef_order[ch] / 2; p++) {
            double tmp = encoder->params_double[ch][p];
            encoder->params_double[ch][p] = encoder->params_double[ch][encoder->coef_order[ch] - p - 1];
            encoder->params_double[ch][encoder->coef_order[ch] - p - 1] = tmp;
        }
        ret = LPC_QuantizeCoefficients(encoder->params_double[ch], encoder->coef_order[ch],
            SRLA_LPC_COEFFICIENT_BITWIDTH, (1 << SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH),
            encoder->params_int[ch], &encoder->rshifts[ch]);
        SRLA_ASSERT(ret == LPC_APIRESULT_OK);
    }

    /* チャンネル毎にLPC予測 */
    for (ch = 0; ch < header->num_channels; ch++) {
        /* LPC予測 */
        SRLALPC_Predict(encoder->buffer_int[ch],
            num_samples, encoder->params_int[ch], encoder->coef_order[ch], encoder->residual[ch], encoder->rshifts[ch]);
    }

    /* ビットライタ作成 */
    BitWriter_Open(&writer, data, data_size);

    /* マルチチャンネル処理法の書き込み */
    SRLA_ASSERT(ch_process_method != SRLA_CH_PROCESS_METHOD_INVALID);
    SRLA_ASSERT(ch_process_method < 4);
    BitWriter_PutBits(&writer, ch_process_method, 2);

    /* パラメータ符号化 */
    /* プリエンファシス */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t p, uval;
        for (p = 0; p < SRLA_NUM_PREEMPHASIS_FILTERS; p++) {
            /* プリエンファシスフィルタのバッファ */
            uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->pre_emphasis_prev[ch][p]);
            SRLA_ASSERT(uval < (1U << (header->bits_per_sample + 1)));
            BitWriter_PutBits(&writer, uval, header->bits_per_sample + 1);
            /* プリエンファシス係数は正値に制限しているため1bitケチれる */
            SRLA_ASSERT(encoder->pre_emphasis[ch][p].coef >= 0);
            uval = (uint32_t)encoder->pre_emphasis[ch][p].coef;
            SRLA_ASSERT(uval < (1U << (SRLA_PREEMPHASIS_COEF_SHIFT - 1)));
            BitWriter_PutBits(&writer, uval, SRLA_PREEMPHASIS_COEF_SHIFT - 1);
        }
    }
    /* LPC係数次数/LPC係数右シフト量/LPC係数 */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t i, uval;
        /* LPC係数次数 */
        SRLA_ASSERT(encoder->coef_order[ch] > 0);
        SRLA_ASSERT(encoder->coef_order[ch] <= (1U << SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH));
        BitWriter_PutBits(&writer, encoder->coef_order[ch] - 1, SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH);
        /* LPC係数右シフト量 */
        SRLA_ASSERT(encoder->rshifts[ch] < (1U << SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH));
        BitWriter_PutBits(&writer, encoder->rshifts[ch], SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH);
        /* LPC係数 */
        for (i = 0; i < encoder->coef_order[ch]; i++) {
            uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->params_int[ch][i]);
            SRLA_ASSERT(uval < (1U << SRLA_LPC_COEFFICIENT_BITWIDTH));
            BitWriter_PutBits(&writer, uval, SRLA_LPC_COEFFICIENT_BITWIDTH);
        }
    }

    /* 残差符号化 */
    for (ch = 0; ch < header->num_channels; ch++) {
        SRLACoder_Encode(encoder->coder, &writer, encoder->residual[ch], num_samples);
    }

    /* バイト境界に揃える */
    BitStream_Flush(&writer);

    /* 書き込みサイズの取得 */
    BitStream_Tell(&writer, (int32_t *)output_size);

    /* ビットライタ破棄 */
    BitStream_Close(&writer);

    return SRLA_APIRESULT_OK;
}

/* 無音データブロックエンコード */
static SRLAApiResult SRLAEncoder_EncodeSilentData(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(num_samples > 0);
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(data_size > 0);
    SRLA_ASSERT(output_size != NULL);

    /* データサイズなし */
    (*output_size) = 0;
    return SRLA_APIRESULT_OK;
}

/* 単一データブロックエンコード */
SRLAApiResult SRLAEncoder_EncodeBlock(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint8_t *data_ptr;
    const struct SRLAHeader *header;
    SRLABlockDataType block_type;
    SRLAApiResult ret;
    uint32_t block_header_size, block_data_size;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL) || (num_samples == 0)
            || (data == NULL) || (data_size == 0) || (output_size == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }
    header = &(encoder->header);

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return SRLA_APIRESULT_PARAMETER_NOT_SET;
    }

    /* エンコードサンプル数チェック */
    if (num_samples > header->num_samples_per_block) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 圧縮手法の判定 */
    block_type = SRLAEncoder_DecideBlockDataType(encoder, input, num_samples);
    SRLA_ASSERT(block_type != SRLA_BLOCK_DATA_TYPE_INVALID);

    /* ブロックヘッダをエンコード */
    data_ptr = data;
    /* ブロック先頭の同期コード */
    ByteArray_PutUint16BE(data_ptr, SRLA_BLOCK_SYNC_CODE);
    /* ブロックサイズ: 仮値で埋めておく */
    ByteArray_PutUint32BE(data_ptr, 0);
    /* ブロックチェックサム: 仮値で埋めておく */
    ByteArray_PutUint16BE(data_ptr, 0);
    /* ブロックデータタイプ */
    ByteArray_PutUint8(data_ptr, block_type);
    /* ブロックチャンネルあたりサンプル数 */
    ByteArray_PutUint16BE(data_ptr, num_samples);
    /* ブロックヘッダサイズ */
    block_header_size = (uint32_t)(data_ptr - data);

    /* データ部のエンコード */
    /* 手法によりエンコードする関数を呼び分け */
    switch (block_type) {
    case SRLA_BLOCK_DATA_TYPE_RAWDATA:
        ret = SRLAEncoder_EncodeRawData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    case SRLA_BLOCK_DATA_TYPE_COMPRESSDATA:
        ret = SRLAEncoder_EncodeCompressData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    case SRLA_BLOCK_DATA_TYPE_SILENT:
        ret = SRLAEncoder_EncodeSilentData(encoder, input, num_samples,
                data_ptr, data_size - block_header_size, &block_data_size);
        break;
    default:
        ret = SRLA_APIRESULT_INVALID_FORMAT;
        break;
    }

    /* エンコードに失敗している */
    if (ret != SRLA_APIRESULT_OK) {
        return ret;
    }

    /* ブロックサイズ書き込み:
    * チェックサム(2byte) + ブロックチャンネルあたりサンプル数(2byte) + ブロックデータタイプ(1byte) */
    ByteArray_WriteUint32BE(&data[2], block_data_size + 5);

    /* チェックサムの領域以降のチェックサムを計算し書き込み */
    {
        /* ブロックチャンネルあたりサンプル数(2byte) + ブロックデータタイプ(1byte) を加算 */
        const uint16_t checksum = SRLAUtility_CalculateFletcher16CheckSum(&data[8], block_data_size + 3);
        ByteArray_WriteUint16BE(&data[6], checksum);
    }

    /* 出力サイズ */
    (*output_size) = block_header_size + block_data_size;

    /* エンコード成功 */
    return SRLA_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をエンコード */
SRLAApiResult SRLAEncoder_EncodeWhole(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    SRLAApiResult ret;
    uint32_t progress, ch, write_size, write_offset, num_encode_samples;
    uint8_t *data_pos;
    const int32_t *input_ptr[SRLA_MAX_NUM_CHANNELS];
    const struct SRLAHeader *header;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL)
            || (data == NULL) || (output_size == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return SRLA_APIRESULT_PARAMETER_NOT_SET;
    }

    /* 書き出し位置を取得 */
    data_pos = data;

    /* ヘッダエンコード */
    encoder->header.num_samples = num_samples;
    if ((ret = SRLAEncoder_EncodeHeader(&(encoder->header), data_pos, data_size))
            != SRLA_APIRESULT_OK) {
        return ret;
    }
    header = &(encoder->header);

    /* 進捗状況初期化 */
    progress = 0;
    write_offset = SRLA_HEADER_SIZE;
    data_pos = data + SRLA_HEADER_SIZE;

    /* ブロックを時系列順にエンコード */
    while (progress < num_samples) {

        /* エンコードサンプル数の確定 */
        num_encode_samples
            = SRLAUTILITY_MIN(header->num_samples_per_block, num_samples - progress);

        /* サンプル参照位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            input_ptr[ch] = &input[ch][progress];
        }

        /* ブロックエンコード */
        if ((ret = SRLAEncoder_EncodeBlock(encoder,
                        input_ptr, num_encode_samples,
                        data_pos, data_size - write_offset, &write_size)) != SRLA_APIRESULT_OK) {
            return ret;
        }

        /* 進捗更新 */
        data_pos      += write_size;
        write_offset  += write_size;
        progress      += num_encode_samples;
        SRLA_ASSERT(write_offset <= data_size);
    }

    /* 成功終了 */
    (*output_size) = write_offset;
    return SRLA_APIRESULT_OK;
}
