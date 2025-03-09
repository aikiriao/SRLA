#include "ltp.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <assert.h>

#include "fft.h"

/* メモリアラインメント */
#define LTP_ALIGNMENT 16
/* 円周率 */
#define LTP_PI 3.1415926535897932384626433832795029
/* ピッチ候補数 */
#define LTP_MAX_NUM_PITCH_CANDIDATES 20
/* 最大自己相関値からどの比率のピークをピッチとして採用するか */
/* 純粋に相関除去したいなら1.0とする */
#define LTP_PITCH_RATIO_VS_MAX_THRESHOULD 1.0

/* nの倍数切り上げ */
#define LTP_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 符号関数 */
#define LTP_SIGN(val) (((val) > 0) - ((val) < 0))
/* 最大値の取得 */
#define LTP_MAX(a,b) (((a) > (b)) ? (a) : (b))
/* 絶対値の取得 */
#define LTP_ABS(val) (((val) > 0) ? (val) : -(val))

/* 内部エラー型 */
typedef enum LTPErrorTag {
    LTP_ERROR_OK = 0,
    LTP_ERROR_NG,
    LTP_ERROR_FAILED_TO_FIND_PITCH,
    LTP_ERROR_SINGULAR_MATRIX,
    LTP_ERROR_INVALID_ARGUMENT
} LTPError;

/* LTP計算ハンドル */
struct LTPCalculator {
    uint32_t max_order; /* 最大次数 */
    uint32_t max_num_buffer_samples; /* 最大バッファサンプル数 */
    uint32_t max_pitch_period; /* 最大周期 */
    double *coef_buffer; /* 係数バッファ */
    double *auto_corr; /* 自己相関 */
    double **auto_corr_mat; /* 自己相関行列 */
    double *buffer; /* 入力信号のバッファ領域 */
    double *work_buffer; /* 計算用バッファ */
    uint8_t alloced_by_own; /* 自分で領域確保したか？ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* 2の冪乗数に切り上げる */
static uint32_t LTP_RoundUp2Powered(uint32_t val)
{
    /* ハッカーのたのしみ参照 */
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    return val + 1;
}

/* LTP係数計算ハンドルのワークサイズ計算 */
int32_t LTPCalculator_CalculateWorkSize(const struct LTPCalculatorConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_order == 0) || (config->max_num_samples == 0)
        || (config->max_pitch_period == 0) || (config->max_pitch_period >= config->max_num_samples)) {
        return -1;
    }

    work_size = sizeof(struct LTPCalculator) + LTP_ALIGNMENT;
    /* 係数領域 */
    work_size += (int32_t)(sizeof(double) * config->max_order);
    /* 自己相関の領域 */
    work_size += (int32_t)(sizeof(double) * (config->max_pitch_period + 1));
    /* 自己相関行列領域 */
    work_size += (int32_t)(sizeof(double *) * (config->max_order + 1));
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1) * (config->max_order + 1));
    /* 入力信号バッファ領域 */
    work_size += (int32_t)(sizeof(double) * LTP_RoundUp2Powered(config->max_num_samples));
    /* 計算用バッファ領域 */
    work_size += (int32_t)(sizeof(double) * LTP_RoundUp2Powered(config->max_num_samples));

    return work_size;
}

/* LTP係数計算ハンドルの作成 */
struct LTPCalculator* LTPCalculator_Create(const struct LTPCalculatorConfig *config, void *work, int32_t work_size)
{
    struct LTPCalculator *ltpc;
    uint8_t *work_ptr;
    uint8_t tmp_alloc_by_own = 0;

    /* 引数チェック */
    if (config == NULL) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_order == 0) || (config->max_num_samples == 0)
        || (config->max_pitch_period == 0) || (config->max_pitch_period >= config->max_num_samples)) {
        return NULL;
    }

    /* 自前でワーク領域確保 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = LTPCalculator_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < LTPCalculator_CalculateWorkSize(config))) {
        if (tmp_alloc_by_own == 1) {
            free(work);
        }
        return NULL;
    }

    /* ワーク領域取得 */
    work_ptr = (uint8_t *)work;

    /* ハンドル領域確保 */
    work_ptr = (uint8_t *)LTP_ROUNDUP((uintptr_t)work_ptr, LTP_ALIGNMENT);
    ltpc = (struct LTPCalculator *)work_ptr;
    work_ptr += sizeof(struct LTPCalculator);

    /* ハンドルメンバの設定 */
    ltpc->max_order = config->max_order;
    ltpc->max_num_buffer_samples = config->max_num_samples;
    ltpc->max_pitch_period = config->max_pitch_period;
    ltpc->work = work;
    ltpc->alloced_by_own = tmp_alloc_by_own;

    /* 係数の領域割当 */
    ltpc->coef_buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * config->max_order;

    /* 自己相関の領域割当 */
    ltpc->auto_corr = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_pitch_period + 1);

    /* 自己相関行列の領域割当 */
    {
        uint32_t ord;
        ltpc->auto_corr_mat = (double **)work_ptr;
        work_ptr += sizeof(double *) * (config->max_order + 1);
        for (ord = 0; ord < config->max_order + 1; ord++) {
            ltpc->auto_corr_mat[ord] = (double *)work_ptr;
            work_ptr += sizeof(double) * (config->max_order + 1);
        }
    }

    /* 入力信号バッファの領域 */
    ltpc->buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * LTP_RoundUp2Powered(config->max_num_samples);

    /* 計算用バッファの領域 */
    ltpc->work_buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * LTP_RoundUp2Powered(config->max_num_samples);

    /* バッファオーバーフローチェック */
    assert((work_ptr - (uint8_t *)work) <= work_size);

    return ltpc;
}

/* LTP係数計算ハンドルの破棄 */
void LTPCalculator_Destroy(struct LTPCalculator *ltpc)
{
    if (ltpc != NULL) {
        /* ワーク領域を時前確保していたときは開放 */
        if (ltpc->alloced_by_own == 1) {
            free(ltpc->work);
        }
    }
}

/* 窓関数の適用 */
static LTPError LTP_ApplyWindow(
        LTPWindowType window_type, const double *input, uint32_t num_samples, double *output)
{
    /* 引数チェック */
    if (input == NULL || output == NULL) {
        return LTP_ERROR_INVALID_ARGUMENT;
    }

    switch (window_type) {
        case LTP_WINDOWTYPE_RECTANGULAR:
            memcpy(output, input, sizeof(double) * num_samples);
            break;
        case LTP_WINDOWTYPE_SIN:
            {
                uint32_t smpl;
                for (smpl = 0; smpl < num_samples; smpl++) {
                    output[smpl] = input[smpl] * sin((LTP_PI * smpl) / (num_samples - 1));
                }
            }
            break;
        case LTP_WINDOWTYPE_WELCH:
            {
                uint32_t smpl;
                const double divisor = 4.0 * pow(num_samples - 1, -2.0);
                for (smpl = 0; smpl < (num_samples >> 1); smpl++) {
                    const double weight = divisor * smpl * (num_samples - 1 - smpl);
                    output[smpl] = input[smpl] * weight;
                    output[num_samples - smpl - 1] = input[num_samples - smpl - 1] * weight;
                }
            }
            break;
        default:
            return LTP_ERROR_NG;
    }

    return LTP_ERROR_OK;
}

/* 窓の二乗和の逆数計算 */
static double LTP_ComputeWindowInverseSquaredSum(LTPWindowType window_type, const uint32_t num_samples)
{
    switch (window_type) {
        case LTP_WINDOWTYPE_RECTANGULAR:
            return 1.0;
        case LTP_WINDOWTYPE_WELCH:
            {
                const double n = num_samples - 1;
                return (15 * (n - 1) * (n - 1) * (n - 1)) / (8 * n * (n - 2) * (n * n - 2 * n + 2));
            }
        default:
            assert(0);
    }

    return 1.0;
}

/* FFTによる（標本）自己相関の計算 data_bufferの内容は破壊される */
static LTPError LTP_CalculateAutoCorrelationByFFT(
    double *data_buffer, double *work_buffer, uint32_t num_buffer_samples, uint32_t num_samples, double *auto_corr, uint32_t order)
{
    uint32_t i;
    uint32_t fft_size;
    const double norm_factor = 2.0 / num_samples;

    assert(num_samples >= order);
    assert(num_buffer_samples >= num_samples);

    /* 引数チェック */
    if ((data_buffer == NULL) || (auto_corr == NULL)) {
        return LTP_ERROR_INVALID_ARGUMENT;
    }

    /* FFTサイズの確定 */
    fft_size = LTP_RoundUp2Powered(num_samples);
    assert(num_buffer_samples >= fft_size);

    /* 後半0埋め */
    for (i = num_samples; i < fft_size; i++) {
        data_buffer[i] = 0.0;
    }

    /* FFT */
    FFT_RealFFT(fft_size, -1, data_buffer, work_buffer);

    /* 複素絶対値の2乗計算 */
    data_buffer[0] *= data_buffer[0];
    data_buffer[1] *= data_buffer[1];
    for (i = 2; i < fft_size; i += 2) {
        const double real = data_buffer[i + 0];
        const double imag = data_buffer[i + 1];
        data_buffer[i + 0] = real * real + imag * imag;
        data_buffer[i + 1] = 0.0;
    }

    /* IFFT */
    FFT_RealFFT(fft_size, 1, data_buffer, work_buffer);

    /* 正規化定数を戻しつつ結果セット */
    for (i = 0; i < order; i++) {
        auto_corr[i] = data_buffer[i] * norm_factor;
    }

    return LTP_ERROR_OK;
}

/* コレスキー分解 */
static LTPError LTP_CholeskyDecomposition(
        double **Amat, int32_t dim, double *inv_diag)
{
    int32_t i, j, k;
    double sum;

    /* 引数チェック */
    assert((Amat != NULL) && (inv_diag != NULL));

    for (i = 0; i < dim; i++) {
        sum = Amat[i][i];
        for (k = i - 1; k >= 0; k--) {
            sum -= Amat[i][k] * Amat[i][k];
        }
        if (sum <= 0.0) {
            return LTP_ERROR_SINGULAR_MATRIX;
        }
        /* 1.0 / sqrt(sum) は除算により桁落ちするためpowを使用 */
        inv_diag[i] = pow(sum, -0.5);
        for (j = i + 1; j < dim; j++) {
            sum = Amat[i][j];
            for (k = i - 1; k >= 0; k--) {
                sum -= Amat[i][k] * Amat[j][k];
            }
            Amat[j][i] = sum * inv_diag[i];
        }
    }

    return LTP_ERROR_OK;
}

/* コレスキー分解により Amat * xvec = bvec を解く */
static LTPError LTP_SolveByCholeskyDecomposition(
        const double * const* Amat, int32_t dim, double *xvec, const double *bvec, const double *inv_diag)
{
    int32_t i, j;
    double sum;

    /* 引数チェック */
    assert((Amat != NULL) && (inv_diag != NULL) && (bvec != NULL) && (xvec != NULL));

    /* 分解を用いて線形一次方程式を解く */
    for (i = 0; i < dim; i++) {
        sum = bvec[i];
        for (j = i - 1; j >= 0; j--) {
            sum -= Amat[i][j] * xvec[j];
        }
        xvec[i] = sum * inv_diag[i];
    }
    for (i = dim - 1; i >= 0; i--) {
        sum = xvec[i];
        for (j = i + 1; j < dim; j++) {
            sum -= Amat[j][i] * xvec[j];
        }
        xvec[i] = sum * inv_diag[i];
    }

    return LTP_ERROR_OK;
}

/* 簡易ピッチ検出 */
static LTPError LTPCalculator_DetectPitch(const double *auto_corr, uint32_t max_pitch_period, uint32_t *pitch_period)
{
    uint32_t i, num_peak;
    double max_peak;
    uint32_t tmp_pitch_period;
    uint32_t pitch_candidate[LTP_MAX_NUM_PITCH_CANDIDATES] = { 0, };

    assert(auto_corr != NULL);
    assert(pitch_period != NULL);

    max_peak = 0.0;
    num_peak = 0;
    i = 1;
    while ((i < max_pitch_period) && (num_peak < LTP_MAX_NUM_PITCH_CANDIDATES)) {
        uint32_t start, end, j, local_peak_index;
        double local_peak;

        /* 負 -> 正 のゼロクロス点を検索 */
        for (start = i; start < max_pitch_period; start++) {
            if ((auto_corr[start - 1] < 0.0) && (auto_corr[start] > 0.0)) {
                break;
            }
        }

        /* 正 -> 負 のゼロクロス点を検索 */
        for (end = start + 1; end < max_pitch_period; end++) {
            if ((auto_corr[end] > 0.0) && (auto_corr[end + 1] < 0.0)) {
                break;
            }
        }

        /* ローカルピークの探索 */
        /* start, end 間で最大のピークを検索 */
        local_peak_index = 0; local_peak = 0.0;
        for (j = start; j <= end; j++) {
            if ((auto_corr[j] > auto_corr[j - 1]) && (auto_corr[j] > auto_corr[j + 1])) {
                if (auto_corr[j] > local_peak) {
                    local_peak_index = j;
                    local_peak = auto_corr[j];
                }
            }
        }
        /* ローカルピーク（ピッチ候補）があった */
        if (local_peak_index != 0) {
            pitch_candidate[num_peak] = local_peak_index;
            num_peak++;
            /* 最大ピーク値の更新 */
            if (local_peak > max_peak) {
                max_peak = local_peak;
            }
        }

        i = end + 1;
    }

    /* ピッチ候補を1つも発見できず */
    if (num_peak == 0) {
        return LTP_ERROR_FAILED_TO_FIND_PITCH;
    }

    /* ピッチ候補を先頭から見て、最大ピーク値の一定割合以上の値を持つ最初のピークがピッチ */
    for (i = 0; i < num_peak; i++) {
        if (auto_corr[pitch_candidate[i]] >= LTP_PITCH_RATIO_VS_MAX_THRESHOULD * max_peak) {
            break;
        }
    }
    tmp_pitch_period = pitch_candidate[i];

    /* 成功終了 */
    (*pitch_period) = tmp_pitch_period;
    return LTP_ERROR_OK;
}

/* LTP係数を求める */
LTPApiResult LTPCalculator_CalculateLTPCoefficients(
        struct LTPCalculator *ltpc,
        const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
        int32_t *pitch_period, LTPWindowType window_type, double regular_term)
{
    uint32_t tmp_pitch_period;

    /* 引数チェック */
    if ((ltpc == NULL) || (data == NULL)
            || (pitch_period == NULL) || (coef == NULL)) {
        return LTP_APIRESULT_INVALID_ARGUMENT;
    }

    /* タップ数は奇数であることを要求 */
    if (!(coef_order & 1)) {
        return LTP_APIRESULT_INVALID_ARGUMENT;
    }

    /* 最大のタップ数を越えている */
    if (coef_order > ltpc->max_order) {
        return LTP_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 窓関数を適用 */
    if (LTP_ApplyWindow(window_type, data, num_samples, ltpc->buffer) != LTP_ERROR_OK) {
        return LTP_APIRESULT_NG;
    }

    /* 自己相関を計算 */
    if (LTP_CalculateAutoCorrelationByFFT(
            ltpc->buffer, ltpc->work_buffer,
            ltpc->max_num_buffer_samples, num_samples, ltpc->auto_corr, ltpc->max_pitch_period) != LTP_ERROR_OK) {
        return LTP_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* 無音フレーム */
    if (fabs(ltpc->auto_corr[0]) <= FLT_MIN) {
        uint32_t i;
        (*pitch_period) = 0;
        for (i = 0; i < coef_order; i++) {
            coef[i] = 0.0;
        }
        return LTP_APIRESULT_OK;
    }

    /* 無音フレームでなければピッチ検出 */
    if (LTPCalculator_DetectPitch(ltpc->auto_corr, ltpc->max_pitch_period, &tmp_pitch_period) != LTP_ERROR_OK) {
        return LTP_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* ピッチ候補の周期が近すぎる（フィルターで見ると現在-未来のサンプルを参照してしまう） */
    if (tmp_pitch_period < ((coef_order / 2) + 1)) {
        return LTP_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* ロングターム係数の導出 */
    {
        uint32_t j, k;

        /* 0次相関を強調(Ridge正則化) */
        ltpc->auto_corr[0] *= (1.0 + regular_term);

        /* 自己相関行列 */
        /* (i,j)要素にギャップ|i-j|の自己相関 */
        for (j = 0; j < coef_order; j++) {
            for (k = j; k < coef_order; k++) {
                const uint32_t lag = (j >= k) ? (j - k) : (k - j);
                ltpc->auto_corr_mat[j][k]
                    = ltpc->auto_corr_mat[k][j]
                    = ltpc->auto_corr[lag];
            }
        }

        /* コレスキー分解 */
        if (LTP_CholeskyDecomposition(ltpc->auto_corr_mat, coef_order, ltpc->work_buffer) != LTP_ERROR_OK) {
            return LTP_APIRESULT_FAILED_TO_CALCULATION;
        }

        /* 求解 */
        /* 右辺は中心においてピッチ周期の自己相関が入ったベクトル */
        if (LTP_SolveByCholeskyDecomposition(ltpc->auto_corr_mat,
            coef_order, ltpc->coef_buffer, &ltpc->auto_corr[tmp_pitch_period - coef_order / 2], ltpc->work_buffer) != LTP_ERROR_OK) {
            return LTP_APIRESULT_FAILED_TO_CALCULATION;
        }

        /* 得られた係数の収束条件を確認 */
        {
            double coef_sum = 0.0;
            for (j = 0; j < coef_order; j++) {
                coef_sum += fabs(ltpc->coef_buffer[j]);
            }
            if (coef_sum >= 1.0) {
                /* 確実に安定する係数をセット: タップ数1と同様の状態に修正 */
                for (j = 0; j < coef_order; j++) {
                    ltpc->coef_buffer[j] = 0.0;
                }
                ltpc->coef_buffer[coef_order / 2]
                    = ltpc->auto_corr[tmp_pitch_period] / ltpc->auto_corr[0];
            }
        }
    }

    /* 結果を出力にセット */
    memcpy(coef, ltpc->coef_buffer, sizeof(double) * coef_order);
    (*pitch_period) = tmp_pitch_period;

    return LTP_APIRESULT_OK;
}
