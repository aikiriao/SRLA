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
#include "static_huffman.h"
#include "srla_coder.h"

/* ダイクストラ法使用時の巨大な重み */
#define SRLAENCODER_DIJKSTRA_BIGWEIGHT (double)(1UL << 24)

/* ブロック探索に必要なノード数の計算 */
#define SRLAENCODER_CALCULATE_NUM_NODES(num_samples, delta_num_samples) ((SRLAUTILITY_ROUNDUP(num_samples, delta_num_samples) / (delta_num_samples)) + 1)

/* エンコーダハンドル */
struct SRLAEncoder {
    struct SRLAHeader header; /* ヘッダ */
    struct SRLACoder *coder; /* 符号化ハンドル */
    uint32_t max_num_channels; /* バッファチャンネル数 */
    uint32_t max_num_samples_per_block; /* バッファサンプル数 */
    uint32_t min_num_samples_per_block; /* 最小ブロックサンプル数 */
    uint32_t max_num_lookahead_samples; /* 最大先読みサンプル数 */
    uint32_t num_lookahead_samples; /* 先読みサンプル数 */
    uint32_t lb_num_samples_per_block; /* ブロックサンプル数の下限 */
    uint32_t max_num_parameters; /* 最大パラメータ数 */
    uint8_t set_parameter; /* パラメータセット済み？ */
    struct LPCCalculator *lpcc; /* LPC計算ハンドル */
    struct SRLAPreemphasisFilter **pre_emphasis; /* プリエンファシスフィルタ */
    struct SRLAOptimalBlockPartitionCalculator *obpc; /* 最適ブロック分割計算ハンドル */
    /* TODO: パラメータの一時領域なので構造体化する */
    double **lpc_coef_double; /* 各チャンネルのLPC係数(double) */
    int32_t **lpc_coef_int; /* 各チャンネルのLPC係数(int) */
    uint32_t *lpc_coef_rshift; /* 各チャンネルのLPC係数右シフト量 */
    uint32_t *lpc_coef_order; /* 各チャンネルのLPC係数次数 */
    uint32_t *use_sum_coef; /* 各チャンネルでLPC係数を和をとって符号化しているか */
    double **ltp_coef_double; /* 各チャンネルのLTP係数(double) */
    int32_t **ltp_coef_int; /* 各チャンネルのLPC係数(int) */
    uint32_t *ltp_period; /* 各チャンネルのLTP予測周期 */
    int32_t **buffer_int; /* 信号バッファ(int) */
    int32_t **residual; /* 残差信号 */
    int32_t **ms_buffer_int; /* MS信号バッファ */
    int32_t **ms_residual; /* MS残差信号 */
    struct SRLAPreemphasisFilter **ms_pre_emphasis; /* プリエンファシスフィルタ(MS) */
    uint32_t *ms_lpc_coef_rshift; /* LPC係数右シフト量(MS) */
    uint32_t *ms_lpc_lpc_coef_order; /* LPC係数次数(MS) */
    uint32_t *ms_use_sum_coef; /* LPC係数を和をとって符号化しているか(MS) */
    int32_t **ms_lpc_coef_int; /* LPC係数(MS) */
    uint32_t *ms_ltp_period; /* LTP予測周期(MS) */
    int32_t **ms_ltp_coef_int; /* LTP係数(MS) */
    double *buffer_double; /* 信号バッファ(double) */
    double *error_vars; /* 各予測係数の残差分散列 */
    double **multiple_lpc_coefs; /* 各次数の予測係数 */
    uint32_t *partitions_buffer; /* 最適な分割設定の記録領域 */
    struct StaticHuffmanCodes param_codes; /* パラメータ符号化用Huffman符号 */
    struct StaticHuffmanCodes sum_param_codes; /* 和をとったパラメータ符号化用Huffman符号 */
    const struct SRLAParameterPreset *parameter_preset; /* パラメータプリセット */
    uint8_t alloced_by_own; /* 領域を自前確保しているか？ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* 最適ブロック分割探索ハンドル */
struct SRLAOptimalBlockPartitionCalculator {
    uint32_t max_num_nodes; /* ノード数 */
    double **adjacency_matrix; /* 隣接行列 */
    double *cost; /* 最小コスト */
    uint32_t *path; /* パス経路 */
    uint8_t *used_flag; /* 各ノードの使用状態フラグ */
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
    /* ブロックあたり最大サンプル数 */
    if (header->max_num_samples_per_block == 0) {
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
    /* ブロックあたり最大サンプル数 */
    ByteArray_PutUint32BE(data_pos, header->max_num_samples_per_block);
    /* パラメータプリセット */
    ByteArray_PutUint8(data_pos, header->preset);

    /* ヘッダサイズチェック */
    SRLA_ASSERT((data_pos - data) == SRLA_HEADER_SIZE);

    /* 成功終了 */
    return SRLA_APIRESULT_OK;
}

/* 探索ハンドルの作成に必要なワークサイズの計算 */
static int32_t SRLAOptimalBlockPartitionCalculator_CalculateWorkSize(
    uint32_t max_num_samples, uint32_t delta_num_samples)
{
    int32_t work_size;
    uint32_t max_num_nodes;

    /* 最大ノード数の計算 */
    max_num_nodes = SRLAENCODER_CALCULATE_NUM_NODES(max_num_samples, delta_num_samples);

    /* 構造体サイズ */
    work_size = sizeof(struct SRLAOptimalBlockPartitionCalculator) + SRLA_MEMORY_ALIGNMENT;

    /* 隣接行列 */
    work_size += (int32_t)((sizeof(double *) + (sizeof(double) * max_num_nodes) + SRLA_MEMORY_ALIGNMENT) * max_num_nodes);
    /* コスト配列 */
    work_size += (int32_t)(sizeof(double) * max_num_nodes + SRLA_MEMORY_ALIGNMENT);
    /* 経路情報 */
    work_size += (int32_t)(sizeof(uint32_t) * max_num_nodes + SRLA_MEMORY_ALIGNMENT);
    /* ノード使用済みフラグ */
    work_size += (int32_t)(sizeof(uint8_t) * max_num_nodes + SRLA_MEMORY_ALIGNMENT);

    return work_size;
}

/* 探索ハンドルの作成 */
static struct SRLAOptimalBlockPartitionCalculator *SRLAOptimalBlockPartitionCalculator_Create(
    uint32_t max_num_samples, uint32_t delta_num_samples, void *work, int32_t work_size)
{
    uint32_t tmp_max_num_nodes;
    struct SRLAOptimalBlockPartitionCalculator* obpc;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((max_num_samples < delta_num_samples) || (work == NULL)
        || (work_size < SRLAOptimalBlockPartitionCalculator_CalculateWorkSize(max_num_samples, delta_num_samples))) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* エンコーダハンドル領域確保 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    obpc = (struct SRLAOptimalBlockPartitionCalculator *)work_ptr;
    work_ptr += sizeof(struct SRLAOptimalBlockPartitionCalculator);

    /* 最大ノード数の計算 */
    tmp_max_num_nodes = SRLAENCODER_CALCULATE_NUM_NODES(max_num_samples, delta_num_samples);
    obpc->max_num_nodes = tmp_max_num_nodes;

    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    obpc->adjacency_matrix = (double **)work_ptr;
    work_ptr += sizeof(struct SRLAOptimalBlockPartitionCalculator);

    /* 領域確保 */
    /* 隣接行列 */
    SRLA_ALLOCATE_2DIMARRAY(obpc->adjacency_matrix, work_ptr, double, tmp_max_num_nodes, tmp_max_num_nodes);
    /* コスト配列 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    obpc->cost = (double *)work_ptr;
    work_ptr += sizeof(double) * tmp_max_num_nodes;
    /* 経路配列 */
    work_ptr = (uint8_t*)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    obpc->path = (uint32_t*)work_ptr;
    work_ptr += sizeof(uint32_t) * tmp_max_num_nodes;
    /* ノード使用済みフラグ */
    work_ptr = (uint8_t*)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    obpc->used_flag = (uint8_t*)work_ptr;
    work_ptr += sizeof(uint8_t) * tmp_max_num_nodes;

    return obpc;
}

/* 探索ハンドルの作成 */
static void SRLAOptimalBlockPartitionCalculator_Destroy(struct SRLAOptimalBlockPartitionCalculator *obpc)
{
    /* 特に何もしない */
    SRLAUTILITY_UNUSED_ARGUMENT(obpc);
}

/* ダイクストラ法により最短経路を求める */
static SRLAError SRLAOptimalBlockPartitionCalculator_ApplyDijkstraMethod(
    struct SRLAOptimalBlockPartitionCalculator *obpc,
    uint32_t num_nodes, uint32_t start_node, uint32_t goal_node, double *min_cost)
{
    uint32_t i, target;
    double min;

    /* 引数チェック */
    if (obpc == NULL || (num_nodes > obpc->max_num_nodes)) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    /* フラグと経路をクリア, 距離は巨大値に設定 */
    for (i = 0; i < obpc->max_num_nodes; i++) {
        obpc->used_flag[i] = 0;
        obpc->path[i] = ~0U;
        obpc->cost[i] = SRLAENCODER_DIJKSTRA_BIGWEIGHT;
    }

    /* ダイクストラ法実行 */
    obpc->cost[start_node] = 0.0;
    target = start_node;
    while (1) {
        /* まだ未確定のノードから最も距離（重み）が小さいノードを
        * その地点の最小距離として確定 */
        min = SRLAENCODER_DIJKSTRA_BIGWEIGHT;
        for (i = 0; i < num_nodes; i++) {
            if ((obpc->used_flag[i] == 0) && (min > obpc->cost[i])) {
                min = obpc->cost[i];
                target = i;
            }
        }

        /* 最短経路が確定 */
        if (target == goal_node) {
            break;
        }

        /* 現在確定したノードから、直接繋がっており、かつ、未確定の
        * ノードに対して、現在確定したノードを経由した時の距離を計算し、
        * 今までの距離よりも小さければ距離と経路を修正 */
        for (i = 0; i < num_nodes; i++) {
            if (obpc->cost[i] > (obpc->adjacency_matrix[target][i] + obpc->cost[target])) {
                obpc->cost[i] = obpc->adjacency_matrix[target][i] + obpc->cost[target];
                obpc->path[i] = target;
            }
        }

        /* 現在注目しているノードを確定に変更 */
        obpc->used_flag[target] = 1;
    }

    /* 最小コストのセット */
    if (min_cost != NULL) {
        (*min_cost) = obpc->cost[goal_node];
    }

    return SRLA_ERROR_OK;
}

/* 最適なブロック分割の探索 */
static SRLAError SRLAEncoder_SearchOptimalBlockPartitions(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_lookahead_samples,
    uint32_t min_num_block_samples, uint32_t max_num_block_samples,
    uint32_t *optimal_num_partitions, uint32_t *optimal_block_partition)
{
    uint32_t i, j, ch;
    uint32_t num_channels, num_nodes, tmp_optimal_num_partitions, tmp_node;
    struct SRLAOptimalBlockPartitionCalculator *obpc;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL) || (optimal_num_partitions == NULL)
        || (optimal_block_partition == NULL)) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    /* ブロックサイズのチェック */
    if (min_num_block_samples > max_num_block_samples) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    /* オート変数に受ける */
    obpc = encoder->obpc;
    num_channels = encoder->header.num_channels;

    /* 隣接行列次元（ノード数）の計算 */
    num_nodes = SRLAENCODER_CALCULATE_NUM_NODES(num_lookahead_samples, min_num_block_samples);

    /* 最大ノード数を超えている */
    if (num_nodes > obpc->max_num_nodes) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    /* 隣接行列を一旦巨大値で埋める */
    for (i = 0; i < num_nodes; i++) {
        for (j = 0; j < num_nodes; j++) {
            obpc->adjacency_matrix[i][j] = SRLAENCODER_DIJKSTRA_BIGWEIGHT;
        }
    }

    /* 隣接行列のセット */
    /* (i,j)要素は、i * delta_num_samples から j * delta_num_samples まで
    * エンコードした時のコスト（符号長）が入る */
    for (i = 0; i < num_nodes; i++) {
        for (j = i + 1; j < num_nodes; j++) {
            double code_length;
            const int32_t *data_ptr[SRLA_MAX_NUM_CHANNELS];
            const uint32_t sample_offset = i * min_num_block_samples;
            uint32_t num_block_samples = (j - i) * min_num_block_samples;

            /* 最大ブロックサイズ以上であれば計算スキップ */
            if (num_block_samples > max_num_block_samples) {
                continue;
            }

            /* 端点で飛び出る場合があるので調節 */
            num_block_samples = SRLAUTILITY_MIN(num_block_samples, num_lookahead_samples - sample_offset);

            /* データ参照位置を設定 */
            for (ch = 0; ch < num_channels; ch++) {
                data_ptr[ch] = &input[ch][sample_offset];
            }

            {
                /* エンコードして長さを計測 */
                uint32_t encode_len;

                if (SRLAEncoder_ComputeBlockSize(encoder,
                    data_ptr, num_block_samples, &encode_len) != SRLA_APIRESULT_OK) {
                    return SRLA_ERROR_NG;
                }

                code_length = encode_len;
                /* TODO: 推定長さでも試す */
            }

            /* 隣接行列にセット */
            obpc->adjacency_matrix[i][j] = code_length;
        }
    }

    /* ダイクストラ法を実行 */
    if (SRLAOptimalBlockPartitionCalculator_ApplyDijkstraMethod(
            obpc, num_nodes, 0, num_nodes - 1, NULL) != SRLA_ERROR_OK) {
        return SRLA_ERROR_NG;
    }

    /* 結果の解釈 */
    /* ゴールから開始位置まで逆順に辿り、まずはパスの長さを求める */
    tmp_optimal_num_partitions = 0;
    tmp_node = num_nodes - 1;
    while (tmp_node != 0) {
        /* ノードの巡回順路は昇順になっているはず */
        SRLA_ASSERT(tmp_node > obpc->path[tmp_node]);
        tmp_node = obpc->path[tmp_node];
        tmp_optimal_num_partitions++;
    }

    /* 再度辿り、分割サイズ情報をセットしていく */
    tmp_node = num_nodes - 1;
    for (i = 0; i < tmp_optimal_num_partitions; i++) {
        const uint32_t sample_offset = obpc->path[tmp_node] * min_num_block_samples;
        uint32_t num_block_samples = (tmp_node - obpc->path[tmp_node]) * min_num_block_samples;
        num_block_samples = SRLAUTILITY_MIN(num_block_samples, num_lookahead_samples - sample_offset);

        /* クリッピングしたブロックサンプル数をセット */
        optimal_block_partition[tmp_optimal_num_partitions - i - 1] = num_block_samples;
        tmp_node = obpc->path[tmp_node];
    }

    /* 分割数の設定 */
    (*optimal_num_partitions) = tmp_optimal_num_partitions;

    return SRLA_ERROR_OK;
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
    tmp_header.max_num_samples_per_block = parameter->max_num_samples_per_block;

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
            || (config->min_num_samples_per_block == 0)
            || (config->max_num_lookahead_samples == 0)
            || (config->max_num_channels == 0)) {
        return -1;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters > config->max_num_samples_per_block) {
        return -1;
    }
    /* ブロックサイズ下限が上限を越えている */
    if (config->min_num_samples_per_block > config->max_num_samples_per_block) {
        return -1;
    }
    /* 最大先読みサンプル数が小さい */
    if (config->max_num_lookahead_samples < config->max_num_samples_per_block) {
        return -1;
    }

    /* ハンドル本体のサイズ */
    work_size = sizeof(struct SRLAEncoder) + SRLA_MEMORY_ALIGNMENT;

    /* LPC計算ハンドルのサイズ */
    {
        struct LPCCalculatorConfig lpcc_config;
        lpcc_config.max_num_samples = config->max_num_samples_per_block;
        lpcc_config.max_order = SRLAUTILITY_MAX(config->max_num_parameters, SRLA_LTP_ORDER);
        if ((tmp_work_size = LPCCalculator_CalculateWorkSize(&lpcc_config)) < 0) {
            return -1;
        }
        work_size += tmp_work_size;
    }

    /* 符号化ハンドルのサイズ */
    if ((tmp_work_size = SRLACoder_CalculateWorkSize(config->max_num_samples_per_block)) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* 最適分割探索ハンドルのサイズ */
    if ((tmp_work_size = SRLAOptimalBlockPartitionCalculator_CalculateWorkSize(
            config->max_num_lookahead_samples, config->min_num_samples_per_block)) < 0) {
        return -1;
    }
    work_size += tmp_work_size;

    /* プリエンファシスフィルタのサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* プリエンファシスフィルタのサイズ(MS) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(struct SRLAPreemphasisFilter, 2, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* パラメータバッファ領域 */
    /* LPC係数(int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_parameters);
    /* LPC係数(MS, int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, 2, config->max_num_parameters);
    /* LPC係数(double) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(double, config->max_num_channels, config->max_num_parameters);
    /* 各チャンネルのLPC係数右シフト量 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* MSチャンネルのLPC係数右シフト量 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * 2);
    /* 各チャンネルのLPC係数次数 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* MSチャンネルのLPC係数次数 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * 2);
    /* 各チャンネルLPC係数を和をとって符号化しているかのフラグ */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* MSチャンネルのLPC係数を和をとって符号化しているかのフラグ */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * 2);
    /* LTP係数(int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, SRLA_LTP_ORDER);
    /* LTP係数(MS, int) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, 2, SRLA_LTP_ORDER);
    /* LTP係数(double) */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(double, config->max_num_channels, SRLA_LTP_ORDER);
    /* 各チャンネルのLTP周期 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * config->max_num_channels);
    /* MSチャンネルのLTP周期 */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(uint32_t) * 2);
    /* 信号処理バッファのサイズ */
    work_size += (int32_t)(2 * SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block));
    work_size += (int32_t)(config->max_num_samples_per_block * sizeof(double) + SRLA_MEMORY_ALIGNMENT);
    /* 残差信号のサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, config->max_num_channels, config->max_num_samples_per_block);
    /* MS信号・残差バッファのサイズ */
    work_size += (int32_t)(2 * SRLA_CALCULATE_2DIMARRAY_WORKSIZE(int32_t, 2, config->max_num_samples_per_block));
    /* 残差分散領域のサイズ */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(double) * (config->max_num_parameters + 1));
    /* LPC係数領域のサイズ */
    work_size += (int32_t)SRLA_CALCULATE_2DIMARRAY_WORKSIZE(double, config->max_num_parameters, config->max_num_parameters);
    /* LTP計数領域のサイズ */
    work_size += (int32_t)(SRLA_MEMORY_ALIGNMENT + sizeof(double) * SRLA_LTP_ORDER);
    /* 分割設定記録領域のサイズ */
    work_size += (int32_t)(SRLAENCODER_CALCULATE_NUM_NODES(config->max_num_samples_per_block, config->min_num_samples_per_block) * sizeof(uint32_t) + SRLA_MEMORY_ALIGNMENT);

    return work_size;
}

/* エンコーダハンドル作成 */
struct SRLAEncoder* SRLAEncoder_Create(const struct SRLAEncoderConfig *config, void *work, int32_t work_size)
{
    uint32_t ch, l;
    struct SRLAEncoder *encoder;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t *work_ptr;

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
        || (config->max_num_samples_per_block == 0)) {
        return NULL;
    }

    /* ブロックサイズはパラメータ数より大きくなるべき */
    if (config->max_num_parameters > config->max_num_samples_per_block) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* エンコーダハンドル領域確保 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder = (struct SRLAEncoder *)work_ptr;
    work_ptr += sizeof(struct SRLAEncoder);

    /* メンバを0クリア */
    memset(encoder, 0, sizeof(struct SRLAEncoder));

    /* エンコーダメンバ設定 */
    encoder->set_parameter = 0;
    encoder->alloced_by_own = tmp_alloc_by_own;
    encoder->work = work;
    encoder->max_num_channels = config->max_num_channels;
    encoder->max_num_samples_per_block = config->max_num_samples_per_block;
    encoder->lb_num_samples_per_block = config->min_num_samples_per_block;
    encoder->max_num_lookahead_samples = config->max_num_lookahead_samples;
    encoder->max_num_parameters = config->max_num_parameters;

    /* LPC計算ハンドルの作成 */
    {
        int32_t lpcc_size;
        struct LPCCalculatorConfig lpcc_config;
        lpcc_config.max_num_samples = config->max_num_samples_per_block;
        lpcc_config.max_order = SRLAUTILITY_MAX(config->max_num_parameters, SRLA_LTP_ORDER);
        lpcc_size = LPCCalculator_CalculateWorkSize(&lpcc_config);
        if ((encoder->lpcc = LPCCalculator_Create(&lpcc_config, work_ptr, lpcc_size)) == NULL) {
            return NULL;
        }
        work_ptr += lpcc_size;
    }

    /* 符号化ハンドルの作成 */
    {
        const int32_t coder_size = SRLACoder_CalculateWorkSize(config->max_num_samples_per_block);
        if ((encoder->coder = SRLACoder_Create(config->max_num_samples_per_block, work_ptr, coder_size)) == NULL) {
            return NULL;
        }
        work_ptr += coder_size;
    }

    /* 最適分割探索ハンドルの作成 */
    {
        const int32_t obpc_size
            = SRLAOptimalBlockPartitionCalculator_CalculateWorkSize(
            config->max_num_lookahead_samples, config->min_num_samples_per_block);
        if ((encoder->obpc = SRLAOptimalBlockPartitionCalculator_Create(
                config->max_num_lookahead_samples, config->min_num_samples_per_block, work_ptr, obpc_size)) == NULL) {
            return NULL;
        }
        work_ptr += obpc_size;
    }

    /* プリエンファシスフィルタの作成 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->pre_emphasis,
        work_ptr, struct SRLAPreemphasisFilter, config->max_num_channels, SRLA_NUM_PREEMPHASIS_FILTERS);
    /* MSチャンネル分のプリエンファシスフィルタの作成 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ms_pre_emphasis,
        work_ptr, struct SRLAPreemphasisFilter, 2, SRLA_NUM_PREEMPHASIS_FILTERS);

    /* バッファ領域の確保 全てのポインタをアラインメント */
    /* LPC係数(int) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->lpc_coef_int,
        work_ptr, int32_t, config->max_num_channels, config->max_num_parameters);
    /* LPC係数(MS, int) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ms_lpc_coef_int,
        work_ptr, int32_t, 2, config->max_num_parameters);
    /* LPC係数(double) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->lpc_coef_double,
        work_ptr, double, config->max_num_channels, config->max_num_parameters);
    /* LPC係数右シフト量 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->lpc_coef_rshift = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* LPC係数右シフト量(MS) */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->ms_lpc_coef_rshift = (uint32_t *)work_ptr;
    work_ptr += 2 * sizeof(uint32_t);
    /* LPC係数次数 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->lpc_coef_order = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* LPC係数次数(MS) */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->ms_lpc_lpc_coef_order = (uint32_t *)work_ptr;
    work_ptr += 2 * sizeof(uint32_t);
    /* LPC係数を和をとって符号化しているかのフラグ */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->use_sum_coef = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* LPC係数を和をとって符号化しているかのフラグ(MS) */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->ms_use_sum_coef = (uint32_t *)work_ptr;
    work_ptr += 2 * sizeof(uint32_t);
    /* LTP係数(int) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ltp_coef_int,
        work_ptr, int32_t, config->max_num_channels, SRLA_LTP_ORDER);
    /* LTP係数(MS, int) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ms_ltp_coef_int,
        work_ptr, int32_t, 2, SRLA_LTP_ORDER);
    /* LTP係数(double) */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ltp_coef_double,
        work_ptr, double, config->max_num_channels, SRLA_LTP_ORDER);
    /* LTP周期 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->ltp_period = (uint32_t *)work_ptr;
    work_ptr += config->max_num_channels * sizeof(uint32_t);
    /* LTP周期(MS) */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->ms_ltp_period = (uint32_t *)work_ptr;
    work_ptr += 2 * sizeof(uint32_t);

    /* 信号処理用バッファ領域 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->buffer_int,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);
    SRLA_ALLOCATE_2DIMARRAY(encoder->residual,
            work_ptr, int32_t, config->max_num_channels, config->max_num_samples_per_block);

    /* MS信号処理用バッファ領域 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->ms_buffer_int,
        work_ptr, int32_t, 2, config->max_num_samples_per_block);
    SRLA_ALLOCATE_2DIMARRAY(encoder->ms_residual,
        work_ptr, int32_t, 2, config->max_num_samples_per_block);

    /* 残差分散領域 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->error_vars = (double *)work_ptr;
    work_ptr += (config->max_num_parameters + 1) * sizeof(double);

    /* 全次数のLPC係数 */
    SRLA_ALLOCATE_2DIMARRAY(encoder->multiple_lpc_coefs,
        work_ptr, double, config->max_num_parameters, config->max_num_parameters);

    /* doubleバッファ */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->buffer_double = (double *)work_ptr;
    work_ptr += config->max_num_samples_per_block * sizeof(double);

    /* 分割設定記録領域 */
    work_ptr = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);
    encoder->partitions_buffer = (uint32_t *)work_ptr;
    work_ptr += SRLAENCODER_CALCULATE_NUM_NODES(config->max_num_samples_per_block, config->min_num_samples_per_block) * sizeof(uint32_t);

    /* バッファオーバーランチェック */
    /* 補足）既にメモリを破壊している可能性があるので、チェックに失敗したら落とす */
    SRLA_ASSERT((work_ptr - (uint8_t *)work) <= work_size);

    /* プリエンファシスフィルタ初期化 */
    for (ch = 0; ch < config->max_num_channels; ch++) {
        for (l = 0; l < SRLA_NUM_PREEMPHASIS_FILTERS; l++) {
            SRLAPreemphasisFilter_Initialize(&encoder->pre_emphasis[ch][l]);
        }
    }

    /* ハフマン符号作成 */
    StaticHuffman_ConvertTreeToCodes(SRLA_GetParameterHuffmanTree(), &encoder->param_codes);
    StaticHuffman_ConvertTreeToCodes(SRLA_GetSumParameterHuffmanTree(), &encoder->sum_param_codes);

    return encoder;
}

/* エンコーダハンドルの破棄 */
void SRLAEncoder_Destroy(struct SRLAEncoder *encoder)
{
    if (encoder != NULL) {
        SRLACoder_Destroy(encoder->coder);
        SRLAOptimalBlockPartitionCalculator_Destroy(encoder->obpc);
        LPCCalculator_Destroy(encoder->lpcc);
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

    /* ヘッダに入れない内容でパラメータチェック */
    if ((parameter->min_num_samples_per_block > parameter->max_num_samples_per_block)
        || (parameter->num_lookahead_samples < parameter->max_num_samples_per_block)
        || ((parameter->num_lookahead_samples % parameter->min_num_samples_per_block) != 0)) {
        return SRLA_APIRESULT_INVALID_FORMAT;
    }

    /* エンコーダの容量を越えてないかチェック */
    if ((encoder->max_num_samples_per_block < parameter->max_num_samples_per_block)
        || (encoder->lb_num_samples_per_block > parameter->min_num_samples_per_block)
        || (encoder->max_num_lookahead_samples < parameter->num_lookahead_samples)
        || (encoder->max_num_channels < parameter->num_channels)) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }
    /* ブロックあたり最大サンプル数のセット */
    tmp_header.max_num_samples_per_block = parameter->max_num_samples_per_block;
    /* ブロックあたり最小サンプル数・先読みサンプル数を記録 */
    encoder->min_num_samples_per_block = parameter->min_num_samples_per_block;
    encoder->num_lookahead_samples = parameter->num_lookahead_samples;

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
    const struct SRLAHeader *header;

    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(encoder->set_parameter == 1);

    /* LPCの次数以下の場合は生データとする */
    if (num_samples <= encoder->parameter_preset->max_num_parameters) {
        return SRLA_BLOCK_DATA_TYPE_RAWDATA;
    }

    header = &encoder->header;

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
    const double intmean = mean_abs_error * (1 << (bps - 1)); /* 整数量子化した時の平均値 */
    const double rho = 1.0 / (1.0 + intmean);
    const uint32_t k2 = (uint32_t)SRLAUTILITY_MAX(0, SRLAUtility_Log2(log(0.5127629514) / log(1.0 - rho)));
    const uint32_t k1 = k2 + 1;
    const double k1factor = pow(1.0 - rho, (double)(1 << k1));
    const double k2factor = pow(1.0 - rho, (double)(1 << k2));
    return (1.0 + k1) * (1.0 - k1factor) + (1.0 + k2 + (1.0 / (1.0 - k2factor))) * k1factor;
}

/* 幾何分布のエントロピーを計算 */
static double SRLAEncoder_CalculateGeometricDistributionEntropy(double mean_abs_error, uint32_t bps)
{
    const double min_abs_error = 1e-16; /* 最小絶対値 */
    const double intmean = mean_abs_error * (1 << (bps - 1)); /* 整数量子化した時の平均値 */
    const double rho = 1.0 / (1.0 + intmean);
    const double invrho = 1.0 - rho;

    if (mean_abs_error < min_abs_error) {
        return 0.0;
    }

    return -(invrho * SRLAUtility_Log2(invrho) + rho * SRLAUtility_Log2(rho)) / rho;
}

/* 最適なLPC次数の選択 */
static SRLAError SRLAEncoder_SelectBestLPCOrder(
    const struct SRLAHeader *header, SRLAChannelLPCOrderDecisionTactics tactics,
    const double *input, uint32_t num_samples, const double **coefs, const double *error_vars,
    uint32_t max_lpc_coef_order, uint32_t *best_lpc_coef_order)
{
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(coefs != NULL);
    SRLA_ASSERT(error_vars != NULL);
    SRLA_ASSERT(best_lpc_coef_order != NULL);

    switch (tactics) {
    case SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED:
        /* 最大次数を常に選択 */
        (*best_lpc_coef_order) = max_lpc_coef_order;
        return SRLA_ERROR_OK;
    case SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_SEARCH:
        /* 網羅探索 */
    {
        double minlen, len, mabse;
        uint32_t i, order, smpl, tmp_best_order = 0;

        minlen = FLT_MAX;
        for (order = 1; order <= max_lpc_coef_order; order++) {
            const double *coef = coefs[order - 1];
            mabse = 0.0;
            for (smpl = order; smpl < num_samples; smpl++) {
                double residual = input[smpl];
                for (i = 0; i < order; i++) {
                    residual += coef[i] * input[smpl - i - 1];
                }
                mabse += SRLAUTILITY_ABS(residual);
            }
            /* 残差符号のサイズ 符号化で非負整数化するため2倍 */
            len = SRLAEncoder_CalculateRGRMeanCodeLength(2.0 * mabse / num_samples, header->bits_per_sample) * num_samples;
            /* 係数のサイズ */
            len += SRLA_LPC_COEFFICIENT_BITWIDTH * order;
            if (minlen > len) {
                minlen = len;
                tmp_best_order = order;
            }
        }
        /* 結果を設定 */
        SRLA_ASSERT(tmp_best_order != 0);
        (*best_lpc_coef_order) = tmp_best_order;
        return SRLA_ERROR_OK;
    }
    case SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION:
        /* 最小推定符号長を与える係数次数の探索 */
    {
        uint32_t order, tmp_best_order = 0;
        double len, mabse, minlen = FLT_MAX;

        for (order = 1; order <= max_lpc_coef_order; order++) {
            /* Laplace分布の仮定で残差分散から平均絶対値を推定 */
            mabse = 2.0 * sqrt(error_vars[order] / 2.0); /* 符号化で非負整数化するため2倍 */
            /* 残差符号のサイズ */
            len = SRLAEncoder_CalculateGeometricDistributionEntropy(mabse, header->bits_per_sample) * num_samples;
            /* 係数のサイズ */
            len += SRLA_LPC_COEFFICIENT_BITWIDTH * order;
            if (minlen > len) {
                minlen = len;
                tmp_best_order = order;
            }
        }

        /* 結果を設定 */
        SRLA_ASSERT(tmp_best_order != 0);
        (*best_lpc_coef_order) = tmp_best_order;
        return SRLA_ERROR_OK;
    }
    default:
        SRLA_ASSERT(0);
    }

    return SRLA_ERROR_NG;
}

/* 1チャンネルのパラメータ・符号長計算 TODO: パラメータ構造体にまとめる */
static SRLAError SRLAEncoder_ComputeCoefficientsPerChannel(
    struct SRLAEncoder *encoder,
    int32_t *buffer_int, double *buffer_double, int32_t *residual_int, uint32_t num_samples,
    struct SRLAPreemphasisFilter *pre_emphasis_filters, uint32_t *lpc_coef_order, uint32_t *coef_rshift,
    int32_t *lpc_int_coef, uint32_t *use_sum_coef, int32_t *ltp_int_coef, uint32_t *ltp_period, uint32_t *code_length)
{
    uint32_t smpl, p;
    LPCApiResult ret;
    SRLAError err;
    const struct SRLAHeader *header;
    const struct SRLAParameterPreset *parameter_preset;
    struct SRLAPreemphasisFilter tmp_pre_emphasis_filters[SRLA_NUM_PREEMPHASIS_FILTERS] = { { 0, } };
    uint32_t tmp_lpc_lpc_coef_order;
    uint32_t tmp_lpc_coef_rshift;
    double *double_coef;
    int32_t tmp_lpc_coef_int[SRLA_MAX_COEFFICIENT_ORDER] = { 0, };
    uint32_t tmp_use_sum_coef;
    uint32_t tmp_code_length;
    int32_t tmp_ltp_period;
    double tmp_ltp_coef_double[SRLA_LTP_ORDER] = { 0.0, };
    int32_t tmp_ltp_coef_int[SRLA_LTP_ORDER] = { 0, };

    /* 引数チェック */
    if ((encoder == NULL) || (buffer_int == NULL) || (buffer_double == NULL) || (residual_int == NULL)
        || (pre_emphasis_filters == NULL) || (lpc_coef_order == NULL) || (coef_rshift == NULL) || (lpc_int_coef == NULL)
        || (ltp_int_coef == NULL) || (use_sum_coef == NULL) || (code_length == NULL)) {
        return SRLA_ERROR_INVALID_ARGUMENT;
    }

    header = &(encoder->header);
    parameter_preset = encoder->parameter_preset;

    /* プリエンファシスフィルタ群 */
    {
        const int32_t head = buffer_int[0];
        struct SRLAPreemphasisFilter filter[SRLA_NUM_PREEMPHASIS_FILTERS] = { 0, };
        SRLAPreemphasisFilter_CalculateMultiStageCoefficients(filter, SRLA_NUM_PREEMPHASIS_FILTERS, buffer_int, num_samples);
        for (p = 0; p < SRLA_NUM_PREEMPHASIS_FILTERS; p++) {
            filter[p].prev = head;
            SRLAPreemphasisFilter_Preemphasis(&filter[p], buffer_int, num_samples);
            tmp_pre_emphasis_filters[p].prev = head;
            tmp_pre_emphasis_filters[p].coef = filter[p].coef;
        }
    }

    /* double精度の信号に変換（[-1,1]の範囲に正規化） */
    {
        const double norm_const = pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        for (smpl = 0; smpl < num_samples; smpl++) {
            buffer_double[smpl] = buffer_int[smpl] * norm_const;
        }
    }

    /* LTP係数 */
    if ((ret = LPCCalculator_CalculateLTPCoefficients(encoder->lpcc,
        buffer_double, num_samples,
        SRLA_LTP_MIN_PERIOD, SRLA_LTP_MAX_PERIOD,
        tmp_ltp_coef_double, SRLA_LTP_ORDER, &tmp_ltp_period,
        LPC_WINDOWTYPE_WELCH, SRLA_LPC_RIDGE_REGULARIZATION_PARAMETER)) != LPC_APIRESULT_OK) {
        if (ret == LPT_APIRESULT_FAILED_TO_FIND_PITCH) {
            /* ピッチ成分を見つけられなかった */
            tmp_ltp_period = 0;
        } else {
            return SRLA_ERROR_NG;
        }
    }

    /* ピッチ成分がある場合は係数を量子化・残差計算 */
    if (tmp_ltp_period > 0) {
        const double norm_const = pow(2.0, -(int32_t)(header->bits_per_sample - 1));
        for (p = 0; p < SRLA_LTP_ORDER; p++) {
            /* 安定条件を満たすために1.0未満であることは確定 */
            assert(fabs(tmp_ltp_coef_double[p]) < 1.0);
            /* 整数に丸め込む */
            const double shift_coef = tmp_ltp_coef_double[p] * pow(2.0, SRLA_LTP_COEFFICIENT_BITWIDTH - 1);
            tmp_ltp_coef_int[p] = (int32_t)SRLAUtility_Round(shift_coef);
        }
        /* 畳み込み演算でインデックスが増える方向にしたい都合上パラメータ順序を変転 */
        for (p = 0; p < SRLA_LTP_ORDER / 2; p++) {
            const int32_t tmp = tmp_ltp_coef_int[p];
            tmp_ltp_coef_int[p] = tmp_ltp_coef_int[SRLA_LTP_ORDER - p - 1];
            tmp_ltp_coef_int[SRLA_LTP_ORDER - p - 1] = tmp;
        }
        /* LTPによる予測 残差を差し替え */
        SRLALTP_Predict(
            buffer_int, num_samples, tmp_ltp_coef_int, SRLA_LTP_ORDER,
            tmp_ltp_period, residual_int, SRLA_LTP_COEFFICIENT_BITWIDTH - 1);
        memcpy(buffer_int, residual_int, sizeof(int32_t) * num_samples);
        for (smpl = 0; smpl < num_samples; smpl++) {
            buffer_double[smpl] = buffer_int[smpl] * norm_const;
        }
    }

    /* 最大次数まで係数と誤差分散を計算 */
    if ((ret = LPCCalculator_CalculateMultipleLPCCoefficients(encoder->lpcc,
        buffer_double, num_samples,
        encoder->multiple_lpc_coefs, encoder->error_vars, parameter_preset->max_num_parameters,
        LPC_WINDOWTYPE_WELCH, SRLA_LPC_RIDGE_REGULARIZATION_PARAMETER)) != LPC_APIRESULT_OK) {
        return SRLA_ERROR_NG;
    }

    /* 次数選択 */
    if ((err = SRLAEncoder_SelectBestLPCOrder(header,
        parameter_preset->lpc_order_tactics,
        buffer_double, num_samples, (const double **)encoder->multiple_lpc_coefs, encoder->error_vars,
        parameter_preset->max_num_parameters, &tmp_lpc_lpc_coef_order)) != SRLA_ERROR_OK) {
        return err;
    }

    /* 係数計算・残差計算 */
    if (tmp_lpc_lpc_coef_order > 0) {
        /* 最良の次数をパラメータに設定 */
        double_coef = encoder->multiple_lpc_coefs[tmp_lpc_lpc_coef_order - 1];

        /* SVRによるLPC係数計算 */
        if ((ret = LPCCalculator_CalculateLPCCoefficientsSVR(encoder->lpcc,
            buffer_double, num_samples,
            double_coef, tmp_lpc_lpc_coef_order, parameter_preset->svr_max_num_iterations,
            LPC_WINDOWTYPE_WELCH, SRLA_LPC_RIDGE_REGULARIZATION_PARAMETER,
            parameter_preset->margin_list, parameter_preset->margin_list_size)) != LPC_APIRESULT_OK) {
            return SRLA_ERROR_NG;
        }

        /* LPC係数量子化 */
        if ((ret = LPC_QuantizeCoefficients(double_coef, tmp_lpc_lpc_coef_order,
            SRLA_LPC_COEFFICIENT_BITWIDTH, (1 << SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH),
            tmp_lpc_coef_int, &tmp_lpc_coef_rshift)) != LPC_APIRESULT_OK) {
            return SRLA_ERROR_NG;
        }

        /* 畳み込み演算でインデックスが増える方向にしたい都合上パラメータ順序を変転 */
        for (p = 0; p < tmp_lpc_lpc_coef_order / 2; p++) {
            const int32_t tmp = tmp_lpc_coef_int[p];
            tmp_lpc_coef_int[p] = tmp_lpc_coef_int[tmp_lpc_lpc_coef_order - p - 1];
            tmp_lpc_coef_int[tmp_lpc_lpc_coef_order - p - 1] = tmp;
        }

        /* LPC予測 */
        SRLALPC_Predict(buffer_int,
            num_samples, tmp_lpc_coef_int, tmp_lpc_lpc_coef_order, residual_int, tmp_lpc_coef_rshift);
    } else {
        /* 次数が0の時は計算をスキップし、入力を単純コピー */
        memcpy(residual_int, buffer_int, sizeof(int32_t) * num_samples);
        /* 右シフトも0とする */
        tmp_lpc_coef_rshift = 0;
    }

    /* 符号長計算 */
    tmp_code_length = 0;

    /* 残差符号長 */
    tmp_code_length += SRLACoder_ComputeCodeLength(encoder->coder, residual_int, num_samples);

    /* プリエンファシスフィルタのバッファ/係数 */
    tmp_code_length += header->bits_per_sample + 1;
    for (p = 0; p < SRLA_NUM_PREEMPHASIS_FILTERS; p++) {
        tmp_code_length += SRLA_PREEMPHASIS_COEF_SHIFT + 1;
    }

    /* LPC係数次数/LPC係数右シフト量 */
    tmp_code_length += SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH;
    tmp_code_length += SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH;

    /* 和をとったか/とらないかのフラグ領域 */
    tmp_code_length += 1;

    /* LPC係数領域サイズ計算 */
    if (tmp_lpc_lpc_coef_order > 0) {
        uint32_t coef_code_length = 0, summed_coef_code_length;

        /* 和をとらない形式で符号長計算 */
        for (p = 0; p < tmp_lpc_lpc_coef_order; p++) {
            const uint32_t uval = SRLAUTILITY_SINT32_TO_UINT32(tmp_lpc_coef_int[p]);
            SRLA_ASSERT(uval < STATICHUFFMAN_MAX_NUM_SYMBOLS);
            coef_code_length += encoder->param_codes.codes[uval].bit_count;
        }

        /* 和をとって符号長計算 */
        tmp_use_sum_coef = 1;
        summed_coef_code_length
            = encoder->param_codes.codes[SRLAUTILITY_SINT32_TO_UINT32(tmp_lpc_coef_int[0])].bit_count;
        for (p = 1; p < tmp_lpc_lpc_coef_order; p++) {
            const int32_t summed = tmp_lpc_coef_int[p] + tmp_lpc_coef_int[p - 1];
            const uint32_t uval = SRLAUTILITY_SINT32_TO_UINT32(summed);
            if (uval >= STATICHUFFMAN_MAX_NUM_SYMBOLS) {
                tmp_use_sum_coef = 0;
                break;
            }
            summed_coef_code_length += encoder->sum_param_codes.codes[uval].bit_count;
            if (summed_coef_code_length >= coef_code_length) {
                tmp_use_sum_coef = 0;
                break;
            }
        }

        /* 係数領域サイズ */
        tmp_code_length += (tmp_use_sum_coef) ? summed_coef_code_length : coef_code_length;
    } else {
        /* 次数が0の時は係数領域はない */
        tmp_use_sum_coef = 0;
    }

    /* LTP予測周期 */
    tmp_code_length += SRLA_LTP_PERIOD_BITWIDTH;

    /* LTP係数領域サイズ計算 */
    if (tmp_ltp_period > 0) {
        tmp_code_length += SRLA_LTP_ORDER * SRLA_LTP_COEFFICIENT_BITWIDTH;
    }

    /* 結果の出力 */
    memcpy(pre_emphasis_filters, tmp_pre_emphasis_filters,
        sizeof(struct SRLAPreemphasisFilter) * SRLA_NUM_PREEMPHASIS_FILTERS);
    (*lpc_coef_order) = tmp_lpc_lpc_coef_order;
    (*coef_rshift) = tmp_lpc_coef_rshift;
    if (tmp_lpc_lpc_coef_order > 0) {
        memcpy(lpc_int_coef, tmp_lpc_coef_int, sizeof(int32_t) * tmp_lpc_lpc_coef_order);
    }
    (*use_sum_coef) = tmp_use_sum_coef;
    if (tmp_ltp_period > 0) {
        memcpy(ltp_int_coef, tmp_ltp_coef_int, sizeof(int32_t) * SRLA_LTP_ORDER);
    }
    (*ltp_period) = tmp_ltp_period;
    (*code_length) = tmp_code_length;

    return SRLA_ERROR_OK;
}

/* 圧縮データブロックの係数計算 */
static SRLAApiResult SRLAEncoder_ComputeCoefficients(
    struct SRLAEncoder *encoder, const int32_t *const *input, uint32_t num_samples,
    SRLAChannelProcessMethod *ch_process_method, uint32_t *output_bits)
{
    uint32_t ch, tmp_output_bits = 0;
    const struct SRLAHeader *header;
    SRLAChannelProcessMethod tmp_ch_process_method = SRLA_CH_PROCESS_METHOD_INVALID;
    uint32_t code_length[SRLA_MAX_NUM_CHANNELS] = { 0, };
    uint32_t ms_code_length[2] = { 0, };

    /* 内部関数なので不正な引数はアサートで落とす */
    SRLA_ASSERT(encoder != NULL);
    SRLA_ASSERT(input != NULL);
    SRLA_ASSERT(num_samples > 0);
    SRLA_ASSERT(ch_process_method != NULL);
    SRLA_ASSERT(output_bits != NULL);

    /* ヘッダ取得 */
    header = &(encoder->header);

    /* 入力をバッファにコピー */
    for (ch = 0; ch < header->num_channels; ch++) {
        memcpy(encoder->buffer_int[ch], input[ch], sizeof(int32_t) * num_samples);
        /* バッファサイズより小さい入力のときは、末尾を0埋め */
        if (num_samples < encoder->max_num_samples_per_block) {
            const uint32_t remain = encoder->max_num_samples_per_block - num_samples;
            memset(&encoder->buffer_int[ch][num_samples], 0, sizeof(int32_t) * remain);
        }
    }

    /* MS信号生成・符号長計算 */
    if (header->num_channels >= 2) {
        for (ch = 0; ch < 2; ch++) {
            memcpy(encoder->ms_buffer_int[ch], encoder->buffer_int[ch], sizeof(int32_t) * num_samples);
        }
        SRLAUtility_LRtoMSConversion(encoder->ms_buffer_int, num_samples);
        for (ch = 0; ch < 2; ch++) {
            SRLAError err;
            if ((err = SRLAEncoder_ComputeCoefficientsPerChannel(encoder,
                encoder->ms_buffer_int[ch], encoder->buffer_double, encoder->ms_residual[ch], num_samples,
                encoder->ms_pre_emphasis[ch], &encoder->ms_lpc_lpc_coef_order[ch], &encoder->ms_lpc_coef_rshift[ch],
                encoder->ms_lpc_coef_int[ch], &encoder->ms_use_sum_coef[ch],
                encoder->ms_ltp_coef_int[ch], &encoder->ms_ltp_period[ch],
                &ms_code_length[ch])) != SRLA_ERROR_OK) {
                return SRLA_APIRESULT_NG;
            }
        }
    }
    /* チャンネルごとにパラメータ・符号長計算 */
    for (ch = 0; ch < header->num_channels; ch++) {
        SRLAError err;
        if ((err = SRLAEncoder_ComputeCoefficientsPerChannel(encoder,
            encoder->buffer_int[ch], encoder->buffer_double, encoder->residual[ch], num_samples,
            encoder->pre_emphasis[ch], &encoder->lpc_coef_order[ch], &encoder->lpc_coef_rshift[ch],
            encoder->lpc_coef_int[ch], &encoder->use_sum_coef[ch],
            encoder->ltp_coef_int[ch], &encoder->ltp_period[ch],
            &code_length[ch])) != SRLA_ERROR_OK) {
            return SRLA_APIRESULT_NG;
        }
    }

    /* マルチチャンネルで最小の符号長を選択 */
    if (header->num_channels == 1) {
        /* モノラルでは何もしない */
        tmp_ch_process_method = SRLA_CH_PROCESS_METHOD_NONE;
        tmp_output_bits = code_length[0];
    } else if (header->num_channels >= 2) {
        SRLAChannelProcessMethod argmin;
        uint32_t len[4], min;

        /* LR, MS, LS, SRの中で最も符号長が短いものを選択 */
        SRLA_STATIC_ASSERT((SRLA_CH_PROCESS_METHOD_NONE == 0) && (SRLA_CH_PROCESS_METHOD_MS == 1)
            && (SRLA_CH_PROCESS_METHOD_LS == 2) && (SRLA_CH_PROCESS_METHOD_SR == 3));
        len[SRLA_CH_PROCESS_METHOD_NONE] = code_length[0] + code_length[1];
        len[SRLA_CH_PROCESS_METHOD_MS] = ms_code_length[0] + ms_code_length[1];
        len[SRLA_CH_PROCESS_METHOD_LS] = code_length[0] + ms_code_length[1];
        len[SRLA_CH_PROCESS_METHOD_SR] = code_length[1] + ms_code_length[1];
        min = len[SRLA_CH_PROCESS_METHOD_NONE]; argmin = SRLA_CH_PROCESS_METHOD_NONE;
        for (ch = 1; ch < 4; ch++) {
            if (min > len[ch]) {
                min = len[ch];
                argmin = (SRLAChannelProcessMethod)ch;
            }
        }

        /* 結果を記録 */
        tmp_ch_process_method = argmin;
        tmp_output_bits = min;

        /* 判定結果に応じてLRの結果を差し替える */
        if (tmp_ch_process_method == SRLA_CH_PROCESS_METHOD_MS) {
            int32_t *tmpp;
            for (ch = 0; ch < 2; ch++) {
                memcpy(encoder->pre_emphasis[ch], encoder->ms_pre_emphasis[ch],
                    sizeof(struct SRLAPreemphasisFilter) * SRLA_NUM_PREEMPHASIS_FILTERS);
                encoder->lpc_coef_order[ch] = encoder->ms_lpc_lpc_coef_order[ch];
                encoder->lpc_coef_rshift[ch] = encoder->ms_lpc_coef_rshift[ch];
                memcpy(encoder->lpc_coef_int[ch], encoder->ms_lpc_coef_int[ch],
                    sizeof(int32_t) * encoder->ms_lpc_lpc_coef_order[ch]);
                encoder->use_sum_coef[ch] = encoder->ms_use_sum_coef[ch];
                memcpy(encoder->ltp_coef_int[ch], encoder->ms_ltp_coef_int[ch], sizeof(int32_t) * SRLA_LTP_ORDER);
                encoder->ltp_period[ch] = encoder->ms_ltp_period[ch];
                tmpp = encoder->residual[ch];
                encoder->residual[ch] = encoder->ms_residual[ch];
                encoder->ms_residual[ch] = tmpp;
            }
        } else if ((tmp_ch_process_method == SRLA_CH_PROCESS_METHOD_LS) || (tmp_ch_process_method == SRLA_CH_PROCESS_METHOD_SR)) {
            int32_t *tmpp;
            const uint32_t src_ch = 1; /* S */
            const uint32_t dst_ch = (tmp_ch_process_method == SRLA_CH_PROCESS_METHOD_LS) ? 1 : 0;
            memcpy(encoder->pre_emphasis[dst_ch], encoder->ms_pre_emphasis[src_ch],
                sizeof(struct SRLAPreemphasisFilter) * SRLA_NUM_PREEMPHASIS_FILTERS);
            encoder->lpc_coef_order[dst_ch] = encoder->ms_lpc_lpc_coef_order[src_ch];
            encoder->lpc_coef_rshift[dst_ch] = encoder->ms_lpc_coef_rshift[src_ch];
            memcpy(encoder->lpc_coef_int[dst_ch], encoder->ms_lpc_coef_int[src_ch],
                sizeof(int32_t) * encoder->ms_lpc_lpc_coef_order[src_ch]);
            encoder->use_sum_coef[dst_ch] = encoder->ms_use_sum_coef[src_ch];
            memcpy(encoder->ltp_coef_int[dst_ch], encoder->ms_ltp_coef_int[src_ch], sizeof(int32_t) * SRLA_LTP_ORDER);
            encoder->ltp_period[dst_ch] = encoder->ms_ltp_period[src_ch];
            tmpp = encoder->residual[dst_ch];
            encoder->residual[dst_ch] = encoder->ms_residual[src_ch];
            encoder->ms_residual[src_ch] = tmpp;
        }
    }

    /* マルチチャンネル処理法のサイズを加える */
    tmp_output_bits += 2;

    /* バイト境界に切り上げ */
    tmp_output_bits = SRLAUTILITY_ROUNDUP(tmp_output_bits, 8);

    /* 結果出力 */
    (*ch_process_method) = tmp_ch_process_method;
    (*output_bits) = tmp_output_bits;

    return SRLA_APIRESULT_OK;
}

/* 圧縮データブロックエンコード */
static SRLAApiResult SRLAEncoder_EncodeCompressData(
        struct SRLAEncoder *encoder,
        const int32_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t ch, tmp_code_length;
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

    /* 係数計算 */
    if (SRLAEncoder_ComputeCoefficients(encoder,
        input, num_samples, &ch_process_method, &tmp_code_length) != SRLA_APIRESULT_OK) {
        return SRLA_APIRESULT_NG;
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
        /* プリエンファシスフィルタのバッファ */
        uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->pre_emphasis[ch][0].prev);
        SRLA_ASSERT(uval < (1U << (header->bits_per_sample + 1)));
        BitWriter_PutBits(&writer, uval, header->bits_per_sample + 1);
        for (p = 0; p < SRLA_NUM_PREEMPHASIS_FILTERS; p++) {
            uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->pre_emphasis[ch][p].coef);
            SRLA_ASSERT(uval < (1U << (SRLA_PREEMPHASIS_COEF_SHIFT + 1)));
            BitWriter_PutBits(&writer, uval, SRLA_PREEMPHASIS_COEF_SHIFT + 1);
        }
    }
    /* LPC係数次数/LPC係数右シフト量/LPC係数 */
    for (ch = 0; ch < header->num_channels; ch++) {
        uint32_t i, uval;
        /* LPC係数次数 */
        SRLA_ASSERT(encoder->lpc_coef_order[ch] < (1U << SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH));
        BitWriter_PutBits(&writer, encoder->lpc_coef_order[ch], SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH);
        /* LPC係数右シフト量 */
        SRLA_ASSERT(encoder->lpc_coef_rshift[ch] < (1U << SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH));
        BitWriter_PutBits(&writer, encoder->lpc_coef_rshift[ch], SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH);
        /* LPC係数 */
        BitWriter_PutBits(&writer, encoder->use_sum_coef[ch], 1); /* 和をとって記録したかのフラグ */
        if (!encoder->use_sum_coef[ch]) {
            for (i = 0; i < encoder->lpc_coef_order[ch]; i++) {
                uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->lpc_coef_int[ch][i]);
                SRLA_ASSERT(uval < (1U << SRLA_LPC_COEFFICIENT_BITWIDTH));
                StaticHuffman_PutCode(&encoder->param_codes, &writer, uval);
            }
        } else {
            uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->lpc_coef_int[ch][0]);
            SRLA_ASSERT(uval < (1U << SRLA_LPC_COEFFICIENT_BITWIDTH));
            StaticHuffman_PutCode(&encoder->param_codes, &writer, uval);
            for (i = 1; i < encoder->lpc_coef_order[ch]; i++) {
                const int32_t summed = encoder->lpc_coef_int[ch][i] + encoder->lpc_coef_int[ch][i - 1];
                uval = SRLAUTILITY_SINT32_TO_UINT32(summed);
                SRLA_ASSERT(uval < (1U << SRLA_LPC_COEFFICIENT_BITWIDTH));
                StaticHuffman_PutCode(&encoder->sum_param_codes, &writer, uval);
            }
        }
    }

    /* LTP周期/LTP係数 */
    for (ch = 0; ch < header->num_channels; ch++) {
        const uint32_t coded_period = (encoder->ltp_period[ch] == 0) ? 0 : (encoder->ltp_period[ch] - SRLA_LTP_MIN_PERIOD + 1);
        SRLA_ASSERT(coded_period < (1U << SRLA_LTP_PERIOD_BITWIDTH));
        BitWriter_PutBits(&writer, coded_period, SRLA_LTP_PERIOD_BITWIDTH);
        if (encoder->ltp_period[ch] > 0) {
            uint32_t i;
            for (i = 0; i < SRLA_LTP_ORDER; i++) {
                const uint32_t uval = SRLAUTILITY_SINT32_TO_UINT32(encoder->ltp_coef_int[ch][i]);
                SRLA_ASSERT(uval < (1U << SRLA_LTP_COEFFICIENT_BITWIDTH));
                BitWriter_PutBits(&writer, uval, SRLA_LTP_COEFFICIENT_BITWIDTH);
            }
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

/* 単一データブロックサイズ計算 */
SRLAApiResult SRLAEncoder_ComputeBlockSize(
    struct SRLAEncoder *encoder, const int32_t *const *input, uint32_t num_samples,
    uint32_t *output_size)
{
    uint8_t *data_ptr;
    const struct SRLAHeader *header;
    SRLABlockDataType block_type;
    uint32_t tmp_block_size;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL) || (num_samples == 0)
        || (output_size == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }
    header = &(encoder->header);

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return SRLA_APIRESULT_PARAMETER_NOT_SET;
    }

    /* エンコードサンプル数チェック */
    if (num_samples > header->max_num_samples_per_block) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 圧縮手法の判定 */
    block_type = SRLAEncoder_DecideBlockDataType(encoder, input, num_samples);
    SRLA_ASSERT(block_type != SRLA_BLOCK_DATA_TYPE_INVALID);

COMPUTE_BLOCK_SIZE_START:
    /* ブロックヘッダサイズ */
    tmp_block_size = 11;

    switch (block_type) {
    case SRLA_BLOCK_DATA_TYPE_RAWDATA:
        tmp_block_size += (header->bits_per_sample * num_samples * header->num_channels) / 8;
        break;
    case SRLA_BLOCK_DATA_TYPE_COMPRESSDATA:
    {
        SRLAApiResult ret;
        uint32_t compress_data_size;
        SRLAChannelProcessMethod dummy;
        /* 符号長計算 */
        if ((ret = SRLAEncoder_ComputeCoefficients(
            encoder, input, num_samples, &dummy, &compress_data_size)) != SRLA_APIRESULT_OK) {
            return ret;
        }
        SRLA_ASSERT(compress_data_size % 8 == 0);
        /* エンコードの結果データが増加したら生データブロックに切り替え */
        if (compress_data_size >= (header->bits_per_sample * num_samples * header->num_channels)) {
            block_type = SRLA_BLOCK_DATA_TYPE_RAWDATA;
            goto COMPUTE_BLOCK_SIZE_START;
        }
        /* バイト単位に変換して加算 */
        tmp_block_size += compress_data_size / 8;
    }
        break;
    case SRLA_BLOCK_DATA_TYPE_SILENT:
        /* 0バイト */
        break;
    default:
        SRLA_ASSERT(0);
    }

    /* 結果出力 */
    (*output_size) = tmp_block_size;

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
    if (num_samples > header->max_num_samples_per_block) {
        return SRLA_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* 圧縮手法の判定 */
    block_type = SRLAEncoder_DecideBlockDataType(encoder, input, num_samples);
    SRLA_ASSERT(block_type != SRLA_BLOCK_DATA_TYPE_INVALID);

ENCODING_BLOCK_START:
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
        /* エンコードの結果データが増加したら生データブロックに切り替え */
        if ((8 * block_data_size) >= (header->bits_per_sample * num_samples * header->num_channels)) {
            block_type = SRLA_BLOCK_DATA_TYPE_RAWDATA;
            goto ENCODING_BLOCK_START;
        }
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

/* 最適なブロック分割探索を含めたエンコード */
SRLAApiResult SRLAEncoder_EncodeOptimalPartitionedBlock(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    SRLAApiResult ret;
    uint32_t num_partitions, part, ch;
    uint32_t progress, write_offset, tmp_output_size;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL)
        || (data == NULL) || (output_size == NULL)) {
        return SRLA_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータがセットされてない */
    if (encoder->set_parameter != 1) {
        return SRLA_APIRESULT_PARAMETER_NOT_SET;
    }

    /* 最適なブロック分割の探索 */
    if (SRLAEncoder_SearchOptimalBlockPartitions(
        encoder, input, num_samples,
        encoder->min_num_samples_per_block, encoder->max_num_samples_per_block,
        &num_partitions, encoder->partitions_buffer) != SRLA_ERROR_OK) {
        return SRLA_APIRESULT_NG;
    }
    SRLA_ASSERT(num_partitions > 0);

    /* 分割に従ってエンコード */
    progress = write_offset = 0;
    for (part = 0; part < num_partitions; part++) {
        const uint32_t num_block_samples = encoder->partitions_buffer[part];
        const int32_t *input_ptr[SRLA_MAX_NUM_CHANNELS];
        for (ch = 0; ch < encoder->header.num_channels; ch++) {
            input_ptr[ch] = &input[ch][progress];
        }
        if ((ret = SRLAEncoder_EncodeBlock(encoder,
                input_ptr, num_block_samples, data + write_offset, data_size - write_offset,
                &tmp_output_size)) != SRLA_APIRESULT_OK) {
            return ret;
        }
        write_offset += tmp_output_size;
        progress += num_block_samples;
        SRLA_ASSERT(write_offset <= data_size);
        SRLA_ASSERT(progress <= num_samples);
    }
    SRLA_ASSERT(progress == num_samples);

    /* 成功終了 */
    (*output_size) = write_offset;
    return SRLA_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をエンコード */
SRLAApiResult SRLAEncoder_EncodeWhole(
    struct SRLAEncoder *encoder,
    const int32_t *const *input, uint32_t num_samples,
    uint8_t *data, uint32_t data_size, uint32_t *output_size,
    SRLAEncoder_EncodeBlockCallback encode_callback)
{
    SRLAApiResult ret;
    uint32_t progress, ch, write_size, write_offset, num_encode_samples, num_process_samples;
    uint8_t *data_pos;
    const int32_t *input_ptr[SRLA_MAX_NUM_CHANNELS];
    const struct SRLAHeader *header;
    SRLAApiResult (*encode_function)(
        struct SRLAEncoder* encoder,
        const int32_t* const* input, uint32_t num_samples,
        uint8_t * data, uint32_t data_size, uint32_t * output_size) = NULL;

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

    /* エンコード関数と進捗サンプル数を決定 */
    if (encoder->min_num_samples_per_block == encoder->max_num_samples_per_block) {
        /* 最適なブロック分割を探索する必要がないため、SRLAEncoder_EncodeBlockを使用 */
        encode_function = SRLAEncoder_EncodeBlock;
        num_process_samples = encoder->max_num_samples_per_block;
    } else {
        encode_function = SRLAEncoder_EncodeOptimalPartitionedBlock;
        num_process_samples = encoder->num_lookahead_samples;
    }

    /* 進捗状況初期化 */
    progress = 0;
    write_offset = SRLA_HEADER_SIZE;
    data_pos = data + SRLA_HEADER_SIZE;

    /* ブロックを時系列順にエンコード */
    while (progress < num_samples) {
        /* エンコードサンプル数の確定 */
        num_encode_samples
            = SRLAUTILITY_MIN(num_process_samples, num_samples - progress);

        /* サンプル参照位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            input_ptr[ch] = &input[ch][progress];
        }

        /* ブロックエンコード */
        if ((ret = encode_function(encoder,
            input_ptr, num_encode_samples,
            data_pos, data_size - write_offset, &write_size)) != SRLA_APIRESULT_OK) {
            return ret;
        }

        /* 進捗更新 */
        data_pos += write_size;
        write_offset += write_size;
        progress += num_encode_samples;
        SRLA_ASSERT(write_offset <= data_size);

        /* コールバック関数が登録されていれば実行 */
        if (encode_callback != NULL) {
            encode_callback(num_samples, progress, data_pos - write_size, write_size);
        }
    }

    /* 成功終了 */
    (*output_size) = write_offset;
    return SRLA_APIRESULT_OK;
}
