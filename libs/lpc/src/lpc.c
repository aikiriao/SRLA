#include "lpc.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <assert.h>

#include "fft.h"

/* メモリアラインメント */
#define LPC_ALIGNMENT 16
/* 円周率 */
#define LPC_PI 3.1415926535897932384626433832795029
/* 残差絶対値の最小値 */
#define LPCAF_RESIDUAL_EPSILON 1e-6

/* nの倍数切り上げ */
#define LPC_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 符号関数 */
#define LPC_SIGN(val) (((val) > 0) - ((val) < 0))
/* 最大値の取得 */
#define LPC_MAX(a,b) (((a) > (b)) ? (a) : (b))
/* 絶対値の取得 */
#define LPC_ABS(val) (((val) > 0) ? (val) : -(val))
/* 軟閾値作用素 */
#define LPC_SOFT_THRESHOLD(in, epsilon) (LPC_SIGN(in) * LPC_MAX(LPC_ABS(in) - (epsilon), 0.0))

/* 内部エラー型 */
typedef enum LPCErrorTag {
    LPC_ERROR_OK = 0,
    LPC_ERROR_NG,
    LPC_ERROR_SINGULAR_MATRIX,
    LPC_ERROR_INVALID_ARGUMENT
} LPCError;

/* LPC計算ハンドル */
struct LPCCalculator {
    uint32_t max_order; /* 最大次数 */
    uint32_t max_num_buffer_samples; /* 最大バッファサンプル数 */
    /* 内部的な計算結果は精度を担保するため全てdoubleで持つ */
    /* floatだとサンプル数を増やすと標本自己相関値の誤差に起因して出力の計算結果がnanになる */
    double **a_vecs; /* 各次数の係数ベクトル */
    double *u_vec; /* 計算用ベクトル3 */
    double *v_vec; /* 計算用ベクトル4 */
    double **r_mat; /* 補助関数法/Burg法で使用する行列（(max_order + 1)次） */
    double *auto_corr; /* 標本自己相関 */
    double *parcor_coef; /* PARCOR係数ベクトル */
    double *error_vars; /* 残差分散 */
    double *buffer; /* 入力信号のバッファ領域 */
    double *work_buffer; /* 計算用バッファ */
    uint8_t alloced_by_own; /* 自分で領域確保したか？ */
    void *work; /* ワーク領域先頭ポインタ */
};

/* round関数（C89で定義されていない） */
static double LPC_Round(double d)
{
    return (d >= 0.0) ? floor(d + 0.5) : -floor(-d + 0.5);
}

/* log2関数（C89で定義されていない） */
static double LPC_Log2(double d)
{
#define INV_LOGE2 (1.4426950408889634)  /* 1 / log(2) */
    return log(d) * INV_LOGE2;
#undef INV_LOGE2
}

/* 2の冪乗数に切り上げる */
static uint32_t LPC_RoundUp2Powered(uint32_t val)
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

/* LPC係数計算ハンドルのワークサイズ計算 */
int32_t LPCCalculator_CalculateWorkSize(const struct LPCCalculatorConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    work_size = sizeof(struct LPCCalculator) + LPC_ALIGNMENT;
    /* a_vecで使用する領域 */
    work_size += (int32_t)(sizeof(double *) * (config->max_order + 1));
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1) * (config->max_order + 2));
    /* u, v ベクトル分の領域 */
    work_size += (int32_t)(sizeof(double) * (config->max_order + 2) * 2);
    /* 標本自己相関の領域 */
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1));
    /* LPC係数ベクトルの領域 */
    work_size += (int32_t)(sizeof(double *) * (config->max_order + 1));
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1) * (config->max_order + 1));
    /* PARCOR係数ベクトルの領域 */
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1));
    /* 残差分散の領域 */
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1));
    /* 補助関数法で使用する行列領域 */
    work_size += (int32_t)(sizeof(double *) * (config->max_order + 1));
    work_size += (int32_t)(sizeof(double) * (config->max_order + 1) * (config->max_order + 1));
    /* 入力信号バッファ領域 */
    work_size += (int32_t)(sizeof(double) * LPC_RoundUp2Powered(config->max_num_samples));
    /* 計算用バッファ領域 */
    work_size += (int32_t)(sizeof(double) * LPC_RoundUp2Powered(config->max_num_samples));

    return work_size;
}

/* LPC係数計算ハンドルの作成 */
struct LPCCalculator* LPCCalculator_Create(const struct LPCCalculatorConfig *config, void *work, int32_t work_size)
{
    struct LPCCalculator *lpcc;
    uint8_t *work_ptr;
    uint8_t tmp_alloc_by_own = 0;

    /* 自前でワーク領域確保 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = LPCCalculator_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((uint32_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < LPCCalculator_CalculateWorkSize(config))
            || (config->max_order == 0) || (config->max_num_samples == 0)) {
        if (tmp_alloc_by_own == 1) {
            free(work);
        }
        return NULL;
    }

    /* ワーク領域取得 */
    work_ptr = (uint8_t *)work;

    /* ハンドル領域確保 */
    work_ptr = (uint8_t *)LPC_ROUNDUP((uintptr_t)work_ptr, LPC_ALIGNMENT);
    lpcc = (struct LPCCalculator *)work_ptr;
    work_ptr += sizeof(struct LPCCalculator);

    /* ハンドルメンバの設定 */
    lpcc->max_order = config->max_order;
    lpcc->max_num_buffer_samples = config->max_num_samples;
    lpcc->work = work;
    lpcc->alloced_by_own = tmp_alloc_by_own;

    /* 計算用ベクトルの領域割当 */
    {
        uint32_t ord;
        lpcc->a_vecs = (double **)work_ptr;
        work_ptr += sizeof(double *) * (config->max_order + 1);
        for (ord = 0; ord < config->max_order + 1; ord++) {
            lpcc->a_vecs[ord] = (double *)work_ptr;
            work_ptr += sizeof(double) * (config->max_order + 2); /* a_0, a_k+1を含めるとmax_order+2 */
        }
    }
    lpcc->u_vec = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_order + 2);
    lpcc->v_vec = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_order + 2);

    /* 標本自己相関の領域割当 */
    lpcc->auto_corr = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_order + 1);

    /* PARCOR係数ベクトルの領域割当 */
    lpcc->parcor_coef = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_order + 1);

    /* 残差分散の領域割り当て */
    lpcc->error_vars = (double *)work_ptr;
    work_ptr += sizeof(double) * (config->max_order + 1);

    /* 補助関数法/Burg法で使用する行列領域 */
    {
        uint32_t ord;
        lpcc->r_mat = (double **)work_ptr;
        work_ptr += sizeof(double *) * (config->max_order + 1);
        for (ord = 0; ord < config->max_order + 1; ord++) {
            lpcc->r_mat[ord] = (double *)work_ptr;
            work_ptr += sizeof(double) * (config->max_order + 1);
        }
    }

    /* 入力信号バッファの領域 */
    lpcc->buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * LPC_RoundUp2Powered(config->max_num_samples);

    /* 計算用バッファの領域 */
    lpcc->work_buffer = (double *)work_ptr;
    work_ptr += sizeof(double) * LPC_RoundUp2Powered(config->max_num_samples);

    /* バッファオーバーフローチェック */
    assert((work_ptr - (uint8_t *)work) <= work_size);

    return lpcc;
}

/* LPC係数計算ハンドルの破棄 */
void LPCCalculator_Destroy(struct LPCCalculator *lpcc)
{
    if (lpcc != NULL) {
        /* ワーク領域を時前確保していたときは開放 */
        if (lpcc->alloced_by_own == 1) {
            free(lpcc->work);
        }
    }
}

/* 窓関数の適用 */
static LPCError LPC_ApplyWindow(
    LPCWindowType window_type, const double *input, uint32_t num_samples, double *output)
{
    /* 引数チェック */
    if (input == NULL || output == NULL) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    switch (window_type) {
    case LPC_WINDOWTYPE_RECTANGULAR:
        memcpy(output, input, sizeof(double) * num_samples);
        break;
    case LPC_WINDOWTYPE_SIN:
        {
            uint32_t smpl;
            for (smpl = 0; smpl < num_samples; smpl++) {
                output[smpl] = input[smpl] * sin((LPC_PI * smpl) / (num_samples - 1));
            }
        }
        break;
    case LPC_WINDOWTYPE_WELCH:
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
        return LPC_ERROR_NG;
    }

    return LPC_ERROR_OK;
}

/*（標本）自己相関の計算 */
static LPCError LPC_CalculateAutoCorrelation(
    const double *data, uint32_t num_samples, double *auto_corr, uint32_t order)
{
    uint32_t i, lag;
    double tmp;

    assert(num_samples >= order);

    /* 引数チェック */
    if (data == NULL || auto_corr == NULL) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* 係数初期化 */
    for (lag = 0; lag < order; lag++) {
        auto_corr[lag] = 0.0;
    }

    /* 次数の代わりにデータ側のラグに注目した自己相関係数計算 */
    for (i = 0; i <= num_samples - order; i++) {
        tmp = data[i];
        /* 同じラグを持ったデータ積和を取る */
        for (lag = 0; lag < order; lag++) {
            auto_corr[lag] += tmp * data[i + lag];
        }
    }
    for (; i < num_samples; i++) {
        tmp = data[i];
        for (lag = 0; lag < num_samples - i; lag++) {
            auto_corr[lag] += tmp * data[i + lag];
        }
    }

    return LPC_ERROR_OK;
}

/* FFTによる（標本）自己相関の計算 data_bufferの内容は破壊される */
static LPCError LPC_CalculateAutoCorrelationByFFT(
    double *data_buffer, double *work_buffer, uint32_t num_buffer_samples, uint32_t num_samples, double *auto_corr, uint32_t order)
{
    uint32_t i;
    uint32_t fft_size;
    const double norm_factor = 2.0 / num_samples;

    assert(num_samples >= order);
    assert(num_buffer_samples >= num_samples);

    /* 引数チェック */
    if ((data_buffer == NULL) || (auto_corr == NULL)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* FFTサイズの確定 */
    fft_size = LPC_RoundUp2Powered(num_samples);
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

    return LPC_ERROR_OK;
}

/* Levinson-Durbin再帰計算 */
static LPCError LPC_LevinsonDurbinRecursion(struct LPCCalculator *lpcc,
    const double *auto_corr, uint32_t coef_order, double *parcor_coef, double *error_vars)
{
    uint32_t k, i;
    double gamma; /* 反射係数 */

    /* オート変数にポインタをコピー */
    double **a_vecs = lpcc->a_vecs;
    double *u_vec = lpcc->u_vec;
    double *v_vec = lpcc->v_vec;

    /* 引数チェック */
    if ((lpcc == NULL) || (auto_corr == NULL) || (parcor_coef == NULL)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* 0次自己相関（信号の二乗和）が小さい場合
    * => 係数は全て0として無音出力システムを予測 */
    if (fabs(auto_corr[0]) < FLT_EPSILON) {
        for (i = 0; i < coef_order + 1; i++) {
            parcor_coef[i] = 0.0;
            error_vars[i] = auto_corr[0]; /* 残差分散は入力と同一 */
        }
        for (k = 0; k < coef_order; k++) {
            for (i = 0; i < coef_order + 2; i++) {
                lpcc->a_vecs[k][i] = 0.0;
            }
        }
        return LPC_ERROR_OK;
    }

    /* 最初のステップの係数をセット */
    a_vecs[0][0] = 1.0;
    error_vars[0] = auto_corr[0];
    a_vecs[0][1] = - auto_corr[1] / auto_corr[0];
    a_vecs[0][2] = 0.0;
    parcor_coef[0] = auto_corr[1] / error_vars[0];
    error_vars[1] = error_vars[0] + auto_corr[1] * a_vecs[0][1];
    u_vec[0] = 1.0; u_vec[1] = 0.0;
    v_vec[0] = 0.0; v_vec[1] = 1.0;

    /* 再帰処理 */
    for (k = 1; k < coef_order; k++) {
        const double *a_vec = a_vecs[k - 1];

        gamma = 0.0;
        for (i = 0; i < k + 1; i++) {
            gamma += a_vec[i] * auto_corr[k + 1 - i];
        }
        gamma /= -error_vars[k];
        error_vars[k + 1] = error_vars[k] * (1.0 - gamma * gamma);
        /* 誤差分散（パワー）は非負 */
        assert(error_vars[k + 1] >= 0.0);

        /* 係数の更新 */
        for (i = 0; i < k + 2; i++) {
            a_vecs[k][i] = a_vec[i] + gamma * a_vec[k + 1 - i];
        }
        a_vecs[k][k + 2] = 0.0;
        /* PARCOR係数は反射係数の符号反転 */
        parcor_coef[k] = -gamma;
        /* PARCOR係数の絶対値は1未満（収束条件） */
        assert(fabs(gamma) < 1.0);
    }

    return LPC_ERROR_OK;
}

/* 係数計算の共通関数 */
static LPCError LPC_CalculateCoef(
    struct LPCCalculator *lpcc, const double *data, uint32_t num_samples, uint32_t coef_order,
    LPCWindowType window_type, double regular_term)
{
    /* 引数チェック */
    if (lpcc == NULL) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* 窓関数を適用 */
    if (LPC_ApplyWindow(window_type, data, num_samples, lpcc->buffer) != LPC_ERROR_OK) {
        return LPC_ERROR_NG;
    }

    /* 自己相関を計算 */
#if 0
    if (LPC_CalculateAutoCorrelation(
            lpcc->buffer, num_samples, lpcc->auto_corr, coef_order + 1) != LPC_ERROR_OK) {
        return LPC_ERROR_NG;
    }
#else
    if (LPC_CalculateAutoCorrelationByFFT(
            lpcc->buffer, lpcc->work_buffer, lpcc->max_num_buffer_samples,
            num_samples, lpcc->auto_corr, coef_order + 1) != LPC_ERROR_OK) {
        return LPC_ERROR_NG;
    }
#endif

    /* 入力サンプル数が少ないときは、係数が発散することが多数
    * => 無音データとして扱い、係数はすべて0とする */
    if (num_samples < coef_order) {
        uint32_t i;
        for (i = 0; i < coef_order + 1; i++) {
            lpcc->parcor_coef[i] = 0.0;
        }
        return LPC_ERROR_OK;
    }

    /* 0次相関を強調(Ridge正則化) */
    lpcc->auto_corr[0] *= (1.0 + regular_term);

    /* 再帰計算を実行 */
    if (LPC_LevinsonDurbinRecursion(lpcc, lpcc->auto_corr, coef_order, lpcc->parcor_coef, lpcc->error_vars) != LPC_ERROR_OK) {
        return LPC_ERROR_NG;
    }

    return LPC_ERROR_OK;
}

/* Levinson-Durbin再帰計算によりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficients(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *lpc_coef, uint32_t coef_order,
    LPCWindowType window_type, double regular_term)
{
    /* 引数チェック */
    if ((data == NULL) || (lpc_coef == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 入力サンプル数チェック */
    if (num_samples > lpcc->max_num_buffer_samples) {
        return LPC_APIRESULT_EXCEED_MAX_NUM_SAMPLES;
    }

    /* 係数計算 */
    if (LPC_CalculateCoef(lpcc, data, num_samples, coef_order, window_type, regular_term) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* 計算成功時は結果をコピー */
    memmove(lpc_coef, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);

    return LPC_APIRESULT_OK;
}

/* Levinson-Durbin再帰計算により与えられた次数まで全てのLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateMultipleLPCCoefficients(
    struct LPCCalculator* lpcc,
    const double* data, uint32_t num_samples, double **lpc_coefs, uint32_t max_coef_order,
    LPCWindowType window_type, double regular_term)
{
    uint32_t k;

    /* 引数チェック */
    if ((data == NULL) || (lpc_coefs == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (max_coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 入力サンプル数チェック */
    if (num_samples > lpcc->max_num_buffer_samples) {
        return LPC_APIRESULT_EXCEED_MAX_NUM_SAMPLES;
    }

    /* 係数計算 */
    if (LPC_CalculateCoef(lpcc, data, num_samples, max_coef_order, window_type, regular_term) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* 計算成功時は結果をコピー */
    for (k = 0; k < max_coef_order; k++) {
        memmove(lpc_coefs[k], &lpcc->a_vecs[k][1], sizeof(double) * max_coef_order);
    }

    return LPC_APIRESULT_OK;
}

/* Levinson-Durbin再帰計算により与えられた次数までの残差分散を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateErrorVariances(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *error_vars, uint32_t max_coef_order,
    LPCWindowType window_type, double regular_term)
{
    /* 引数チェック */
    if ((data == NULL) || (error_vars == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (max_coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 入力サンプル数チェック */
    if (num_samples > lpcc->max_num_buffer_samples) {
        return LPC_APIRESULT_EXCEED_MAX_NUM_SAMPLES;
    }

    /* 係数計算 */
    if (LPC_CalculateCoef(lpcc, data, num_samples, max_coef_order, window_type, regular_term) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* 計算成功時は結果をコピー */
    memmove(error_vars, lpcc->error_vars, sizeof(double) * (max_coef_order + 1));

    return LPC_APIRESULT_OK;
}

/* コレスキー分解 */
static LPCError LPC_CholeskyDecomposition(
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
            return LPC_ERROR_SINGULAR_MATRIX;
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

    return LPC_ERROR_OK;
}

/* コレスキー分解により Amat * xvec = bvec を解く */
static LPCError LPC_SolveByCholeskyDecomposition(
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

    return LPC_ERROR_OK;
}

#if 1
/* 補助関数法（前向き残差）による係数行列計算 */
static LPCError LPCAF_CalculateCoefMatrixAndVector(
        const double *data, uint32_t num_samples,
        const double *a_vec, double **r_mat, double *r_vec,
        uint32_t coef_order, double *pobj_value)
{
    double obj_value;
    uint32_t smpl, i, j;

    assert(data != NULL);
    assert(a_vec != NULL);
    assert(r_mat != NULL);
    assert(r_vec != NULL);
    assert(pobj_value != NULL);
    assert(num_samples > coef_order);

    /* 行列を0初期化 */
    for (i = 0; i < coef_order; i++) {
        r_vec[i] = 0.0;
        for (j = 0; j < coef_order; j++) {
            r_mat[i][j] = 0.0;
        }
    }

    obj_value = 0.0;

    for (smpl = coef_order; smpl < num_samples; smpl++) {
        /* 残差計算 */
        double residual = data[smpl];
        double inv_residual;
        for (i = 0; i < coef_order; i++) {
            residual += a_vec[i] * data[smpl - i - 1];
        }
        residual = fabs(residual);
        obj_value += residual;
        /* 小さすぎる残差は丸め込む（ゼERO割回避、正則化） */
        residual = (residual < LPCAF_RESIDUAL_EPSILON) ? LPCAF_RESIDUAL_EPSILON : residual;
        inv_residual = 1.0 / residual;
        /* 係数行列に蓄積 */
        for (i = 0; i < coef_order; i++) {
            r_vec[i] -= data[smpl] * data[smpl - i - 1] * inv_residual;
            for (j = i; j < coef_order; j++) {
                r_mat[i][j] += data[smpl - i - 1] * data[smpl - j - 1] * inv_residual;
            }
        }
    }

    /* 対称要素に拡張 */
    for (i = 0; i < coef_order; i++) {
        for (j = i + 1; j < coef_order; j++) {
            r_mat[j][i] = r_mat[i][j];
        }
    }

    /* 目的関数値のセット */
    (*pobj_value) = obj_value / (num_samples - coef_order);

    return LPC_ERROR_OK;
}
#else
/* 補助関数法（前向き後ろ向き残差）による係数行列計算 */
static LPCError LPCAF_CalculateCoefMatrixAndVector(
        const double *data, uint32_t num_samples,
        const double *a_vec, double **r_mat, double *r_vec,
        uint32_t coef_order, double *pobj_value)
{
    double obj_value;
    uint32_t smpl, i, j;

    assert(data != NULL);
    assert(a_vec != NULL);
    assert(r_mat != NULL);
    assert(r_vec != NULL);
    assert(pobj_value != NULL);
    assert(num_samples > coef_order);

    /* 行列を0初期化 */
    for (i = 0; i < coef_order; i++) {
        r_vec[i] = 0.0;
        for (j = 0; j < coef_order; j++) {
            r_mat[i][j] = 0.0;
        }
    }

    obj_value = 0.0;

    for (smpl = coef_order; smpl < num_samples - coef_order; smpl++) {
        /* 残差計算 */
        double forward = data[smpl], backward = data[smpl];
        double inv_forward, inv_backward;
        for (i = 0; i < coef_order; i++) {
            forward += a_vec[i] * data[smpl - i - 1];
            backward += a_vec[i] * data[smpl + i + 1];
        }
        forward = fabs(forward);
        backward = fabs(backward);
        obj_value += (forward + backward);
        /* 小さすぎる残差は丸め込む（ゼERO割回避、正則化） */
        forward = (forward < LPCAF_RESIDUAL_EPSILON) ? LPCAF_RESIDUAL_EPSILON : forward;
        backward = (backward < LPCAF_RESIDUAL_EPSILON) ? LPCAF_RESIDUAL_EPSILON : backward;
        inv_forward = 1.0 / forward;
        inv_backward = 1.0 / backward;
        /* 係数行列に足し込み */
        for (i = 0; i < coef_order; i++) {
            r_vec[i] -= data[smpl] * data[smpl - i - 1] * inv_forward;
            r_vec[i] -= data[smpl] * data[smpl + i + 1] * inv_backward;
            for (j = i; j < coef_order; j++) {
                r_mat[i][j] += data[smpl - i - 1] * data[smpl - j - 1] * inv_forward;
                r_mat[i][j] += data[smpl + i + 1] * data[smpl + j + 1] * inv_backward;
            }
        }
    }

    /* 対称要素に拡張 */
    for (i = 0; i < coef_order; i++) {
        for (j = i + 1; j < coef_order; j++) {
            r_mat[j][i] = r_mat[i][j];
        }
    }

    (*pobj_value) = obj_value / (2.0 * (num_samples - (2.0 * coef_order)));

    return LPC_ERROR_OK;
}
#endif

/* 補助関数法による係数計算 */
static LPCError LPC_CalculateCoefAF(
        struct LPCCalculator *lpcc, const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
        const uint32_t max_num_iteration, const double obj_epsilon, LPCWindowType window_type, double regular_term)
{
    uint32_t itr, i;
    double *a_vec = lpcc->work_buffer;
    double *r_vec = lpcc->u_vec;
    double **r_mat = lpcc->r_mat;
    double obj_value, prev_obj_value;
    LPCError err;

    /* 係数をLebinson-Durbin法で初期化 */
    if ((err = LPC_CalculateCoef(lpcc, data, num_samples, coef_order, window_type, regular_term)) != LPC_ERROR_OK) {
        return err;
    }
    memcpy(coef, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);

    /* 0次自己相関（信号の二乗和）が小さい場合
    * => 係数は全て0として無音出力システムを予測 */
    if (fabs(lpcc->auto_corr[0]) < FLT_EPSILON) {
        for (i = 0; i < coef_order + 1; i++) {
            lpcc->a_vecs[coef_order - 1][i] = 0.0;
        }
        return LPC_ERROR_OK;
    }

    prev_obj_value = FLT_MAX;
    for (itr = 0; itr < max_num_iteration; itr++) {
        /* 係数行列要素の計算 */
        if ((err = LPCAF_CalculateCoefMatrixAndVector(
                data, num_samples, a_vec, r_mat, r_vec, coef_order, &obj_value)) != LPC_ERROR_OK) {
            return err;
        }
        /* コレスキー分解 */
        if ((err = LPC_CholeskyDecomposition(
                r_mat, (int32_t)coef_order, lpcc->v_vec)) == LPC_ERROR_SINGULAR_MATRIX) {
            /* 特異行列になるのは理論上入力が全部0のとき。係数を0クリアして終わる */
            for (i = 0; i < coef_order; i++) {
                lpcc->a_vecs[coef_order - 1][i] = 0.0;
            }
            return LPC_ERROR_OK;
        }
        /* コレスキー分解で r_mat @ avec = r_vec を解く */
        if ((err = LPC_SolveByCholeskyDecomposition(
                (const double * const *)r_mat, (int32_t)coef_order, coef, r_vec, lpcc->v_vec)) != LPC_ERROR_OK) {
            return err;
        }
        assert(err == LPC_ERROR_OK);
        /* 収束判定 */
        if (fabs(prev_obj_value - obj_value) < obj_epsilon) {
            break;
        }
        prev_obj_value = obj_value;
    }

    return LPC_ERROR_OK;
}

/* 補助関数法よりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsAF(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    uint32_t max_num_iteration, LPCWindowType window_type, double regular_term)
{
    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (coef == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 係数計算 */
    if (LPC_CalculateCoefAF(lpcc, data, num_samples, coef, coef_order,
            max_num_iteration, 1e-8, window_type, regular_term) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    return LPC_APIRESULT_OK;
}

/* Burg法による係数計算 */
static LPCError LPC_CalculateCoefBurg(
        struct LPCCalculator *lpcc, const double *data, uint32_t num_samples, double *coef, uint32_t coef_order)
{
#if 1
    uint32_t i, j, k;
    double *a_vec = lpcc->a_vecs[0];
    double **cov = lpcc->r_mat;
    LPCError err;

    /* 自己共分散行列計算 */
    for (i = 0; i <= coef_order; i++) {
        if ((err = LPC_CalculateAutoCorrelation(
                        data, num_samples - i, &cov[i][i], coef_order + 1 - i)) != LPC_ERROR_OK) {
            return err;
        }
        for (j = i + 1; j <= coef_order; j++) {
            cov[j][i] = cov[i][j];
        }
    }

    /* 係数初期化 */
    for (i = 0; i <= coef_order; i++) {
        a_vec[i] = 0.0;
    }
    a_vec[0] = 1.0;

    /* 次数ごとに計算 */
    for (k = 0; k < coef_order; k++) {
        double mu;
        double FkpBk = 0.0, sum = 0.0, Ck = 0.0;
        /* Fk + Bk */
        for (i = 0; i <= k; i++) {
            FkpBk += a_vec[i] * a_vec[i] * (cov[i][i] + cov[k + 1 - i][k + 1 - i]);
            /* 対角成分以外は対称性を使って半分だけ計算 */
            for (j = i + 1; j <= k; j++) {
                sum += a_vec[i] * a_vec[j] * (cov[i][j] + cov[k + 1 - i][k + 1 - j]);
            }
        }
        FkpBk += 2.0 * sum;
        /* Ck */
        for (i = 0; i <= k; i++) {
            for (j = 0; j <= k; j++) {
                Ck += a_vec[i] * a_vec[j] * cov[i][k + 1 - j];
            }
        }
        /* 反射係数の負号 */
        mu = - 2.0 * Ck / FkpBk;
        assert(fabs(mu) <= 1.0);
        /* 係数更新 */
        for (i = 0; i <= (k + 1) / 2; i++) {
            double tmp1, tmp2;
            tmp1 = a_vec[i]; tmp2 = a_vec[k + 1 - i];
            a_vec[i]         = tmp1 + mu * tmp2;
            a_vec[k + 1 - i] = mu * tmp1 + tmp2;
        }
    }

    /* 解を設定 */
    memcpy(coef, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);
#else
    uint32_t i, k;
    double *a_vec = lpcc->a_vecs[0];
    double *f_vec, *b_vec;
    double Dk, mu;
    double tmp1, tmp2;

    /* ベクトル領域割り当て */
    f_vec = malloc(sizeof(double) * num_samples);
    b_vec = malloc(sizeof(double) * num_samples);

    /* 各ベクトル初期化 */
    for (k = 0; k < coef_order + 1; k++) {
        a_vec[k] = 0.0;
    }
    a_vec[0] = 1.0;
    memcpy(f_vec, data, sizeof(double) * num_samples);
    memcpy(b_vec, data, sizeof(double) * num_samples);

    /* Dkの初期化 */
    Dk = 0.0;
    for (i = 0; i < num_samples; i++) {
        Dk += 2.0 * f_vec[i] * f_vec[i];
    }
    Dk -= f_vec[0] * f_vec[0] + f_vec[num_samples - 1] * f_vec[num_samples - 1];

    /* Burg 再帰アルゴリズム */
    for (k = 0; k < coef_order; k++) {
        /* 反射（PARCOR）係数の計算 */
        mu = 0.0;
        for (i = 0; i < num_samples - k - 1; i++) {
            mu += f_vec[i + k + 1] * b_vec[i];
        }
        mu *= -2.0 / Dk;
        assert(fabs(mu) < 1.0);
        /* a_vecの更新 */
        for (i = 0; i <= (k + 1) / 2; i++) {
            tmp1 = a_vec[i]; tmp2 = a_vec[k + 1 - i];
            a_vec[i]         = tmp1 + mu * tmp2;
            a_vec[k + 1 - i] = mu * tmp1 + tmp2;
        }
        /* f_vec, b_vecの更新 */
        for (i = 0; i < num_samples - k - 1; i++) {
            tmp1 = f_vec[i + k + 1]; tmp2 = b_vec[i];
            f_vec[i + k + 1] = tmp1 + mu * tmp2;
            b_vec[i]         = mu * tmp1 + tmp2;
        }
        /* Dkの更新 */
        Dk = (1.0 - mu * mu) * Dk - f_vec[k + 1] * f_vec[k + 1] - b_vec[num_samples - k - 2] * b_vec[num_samples - k - 2];
    }

    /* 係数コピー */
    memcpy(a_vec, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);

    free(b_vec);
    free(f_vec);
#endif
    return LPC_ERROR_OK;
}

/* Burg法によりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsBurg(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order)
{
    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (coef == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 係数計算 */
    if (LPC_CalculateCoefBurg(lpcc, data, num_samples, coef, coef_order) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    return LPC_APIRESULT_OK;
}

/* 共分散行列の計算 */
static LPCError LPCSVR_CalculateCovarianceMatrix(
    const double *data, uint32_t num_samples, double **cov, uint32_t dim)
{
    uint32_t i, j, smpl;

    /* 引数チェック */
    if ((data == NULL) || (cov == NULL)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    for (i = 0; i < dim; i++) {
        for (j = i; j < dim; j++) {
            cov[i][j] = 0.0;
        }
    }

    for (smpl = 0; smpl < num_samples - dim; smpl++) {
        const double *pdata = &data[smpl];
        for (i = 0; i < dim; i++) {
            const double s = pdata[i];
            for (j = i; j < dim; j++) {
                cov[i][j] += s * pdata[j];
            }
        }
    }

    for (i = 0; i < dim; i++) {
        for (j = i + 1; j < dim; j++) {
            cov[j][i] = cov[i][j];
        }
    }

    return LPC_ERROR_OK;
}

/* Recursive Golomb-Rice符号の平均符号長 */
static double LPCSVR_CalculateRGRMeanCodeLength(double mean_abs_error, uint32_t bps)
{
    const double intmean = mean_abs_error * (1 << bps); /* 整数量子化した時の平均値 */
    const double rho = 1.0 / (1.0 + intmean);
    const uint32_t k2 = (uint32_t)LPC_MAX(0, LPC_Log2(log(0.5127629514) / log(1.0 - rho)));
    const uint32_t k1 = k2 + 1;
    const double k1factor = pow(1.0 - rho, (double)(1 << k1));
    const double k2factor = pow(1.0 - rho, (double)(1 << k2));
    return (1.0 + k1) * (1.0 - k1factor) + (1.0 + k2 + (1.0 / (1.0 - k2factor))) * k1factor;
}

/* SVRによる係数計算 */
static LPCError LPC_CalculateCoefSVR(
    struct LPCCalculator *lpcc, const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    const uint32_t max_num_iteration, const double obj_epsilon, LPCWindowType window_type,
    double regular_term, const double *margin_list, uint32_t margin_list_size)
{
#define BITS_PER_SAMPLE 16
    uint32_t itr, i, j, smpl;
    double *r_vec = lpcc->u_vec;
    double *low = lpcc->v_vec;
    double *best_coef = lpcc->work_buffer;
    double *delta = lpcc->parcor_coef;
    double *init_coef = lpcc->auto_corr;
    double **cov = lpcc->r_mat;
    double *residual = lpcc->buffer;
    double obj_value, prev_obj_value, min_obj_value;
    LPCError err;

    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (margin_list == NULL) || (margin_list_size == 0)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* Levinson-Durbin法で係数を求める */
    if ((err = LPC_CalculateCoef(lpcc, data, num_samples, coef_order, window_type, regular_term)) != LPC_ERROR_OK) {
        return err;
    }
    /* 0次自己相関（信号の二乗和）が小さい場合
    * => 係数は全て0として無音出力システムを予測 */
    if (fabs(lpcc->auto_corr[0]) < FLT_EPSILON) {
        for (i = 0; i < coef_order; i++) {
            coef[i] = 0.0;
        }
        return LPC_ERROR_OK;
    }

    /* 学習しない場合はLevinson-Durbin法の結果をそのまま採用 */
    if (max_num_iteration == 0) {
        memcpy(coef, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);
        return LPC_ERROR_OK;
    }

    /* 共分散行列の計算 */
    if ((err = LPCSVR_CalculateCovarianceMatrix(data, num_samples, cov, coef_order)) != LPC_ERROR_OK) {
        return err;
    }
    /* Ridge正則化 */
    for (i = 0; i < coef_order; i++) {
        cov[i][i] *= (1.0 + regular_term);
    }
    /* コレスキー分解 */
    if ((err = LPC_CholeskyDecomposition(cov, (int32_t)coef_order, low)) == LPC_ERROR_SINGULAR_MATRIX) {
        /* 特異行列になるのは理論上入力が全部0のとき。係数を0クリアして終わる */
        for (i = 0; i < coef_order; i++) {
            coef[i] = 0.0;
        }
        return LPC_ERROR_OK;
    }

    /* 初期値をLevinson-Durbin法の係数に設定 */
    memcpy(init_coef, &lpcc->a_vecs[coef_order - 1][1], sizeof(double) * coef_order);
    memcpy(best_coef, init_coef, sizeof(double) * coef_order);

    /* TODO: 係数は順序反転した方がresidualの計算が早そう（要検証） */

    min_obj_value = FLT_MAX;
    for (j = 0; j < margin_list_size; j++) {
        const double margin = margin_list[j];
        prev_obj_value = FLT_MAX;
        memcpy(coef, init_coef, sizeof(double) * coef_order);
        for (itr = 0; itr < max_num_iteration; itr++) {
            double mabse = 0.0;
            /* 残差計算/残差ソフトスレッショルド */
            memcpy(residual, data, sizeof(double) * num_samples);
            for (i = 0; i < coef_order; i++) {
                r_vec[i] = 0.0;
            }
            for (smpl = coef_order; smpl < num_samples; smpl++) {
                for (i = 0; i < coef_order; i++) {
                    residual[smpl] += coef[i] * data[smpl - i - 1];
                }
                mabse += LPC_ABS(residual[smpl]);
                residual[smpl] = LPC_SOFT_THRESHOLD(residual[smpl], margin);
                for (i = 0; i < coef_order; i++) {
                    r_vec[i] += residual[smpl] * data[smpl - i - 1];
                }
            }
            obj_value = LPCSVR_CalculateRGRMeanCodeLength(mabse / num_samples, BITS_PER_SAMPLE);
            /* コレスキー分解で cov @ delta = r_vec を解く */
            if ((err = LPC_SolveByCholeskyDecomposition(
                    (const double * const *)cov, (int32_t)coef_order, delta, r_vec, low)) != LPC_ERROR_OK) {
                return err;
            }
            /* 最善係数の更新 */
            if (obj_value < min_obj_value) {
                memcpy(best_coef, coef, sizeof(double) * coef_order);
                min_obj_value = obj_value;
            }
            /* 収束判定 */
            if ((prev_obj_value < obj_value) || (fabs(prev_obj_value - obj_value) < obj_epsilon)) {
                break;
            }
            /* 係数更新 */
            for (i = 0; i < coef_order; i++) {
                coef[i] += delta[i];
            }
            prev_obj_value = obj_value;
        }
    }

    memcpy(coef, best_coef, sizeof(double) * coef_order);

    return LPC_ERROR_OK;
#undef BITS_PER_SAMPLE
}

/* SVRよりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsSVR(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    uint32_t max_num_iteration, LPCWindowType window_type,
    double regular_term, const double *margin_list, uint32_t margin_list_size)
{
    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (coef == NULL)
        || (margin_list == NULL) || (margin_list_size == 0)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 係数計算 */
    if (LPC_CalculateCoefSVR(lpcc, data, num_samples, coef, coef_order,
            max_num_iteration, 1e-8, window_type, regular_term, margin_list, margin_list_size) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    return LPC_APIRESULT_OK;
}

/* 入力データからサンプルあたりの推定符号長を求める */
LPCApiResult LPCCalculator_EstimateCodeLength(
        struct LPCCalculator *lpcc,
        const double *data, uint32_t num_samples, uint32_t bits_per_sample,
        uint32_t coef_order, double *length_per_sample_bits, LPCWindowType window_type)
{
    uint32_t ord;
    double log2_mean_res_power, log2_var_ratio;

    /* 定数値 */
#define BETA_CONST_FOR_LAPLACE_DIST   (1.9426950408889634)  /* sqrt(2 * E * E) */
#define BETA_CONST_FOR_GAUSS_DIST     (2.047095585180641)   /* sqrt(2 * E * PI) */

    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (length_per_sample_bits == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* 係数計算 */
    if (LPC_CalculateCoef(lpcc, data, num_samples, coef_order, window_type, 0.0) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* log2(パワー平均)の計算 */
    log2_mean_res_power = lpcc->auto_corr[0]; /* 0次標本自己相関はパワー */
    /* 整数PCMの振幅に変換（doubleの密度保障） */
    log2_mean_res_power *= pow(2, (double)(2.0 * (bits_per_sample - 1)));
    if (fabs(log2_mean_res_power) <= FLT_MIN) {
        /* ほぼ無音だった場合は符号長を0とする */
        (*length_per_sample_bits) = 0.0;
        return LPC_APIRESULT_OK;
    }
    log2_mean_res_power = LPC_Log2((double)log2_mean_res_power) - LPC_Log2((double)num_samples);

    /* sum(log2(1 - (parcor * parcor)))の計算 */
    log2_var_ratio = 0.0;
    for (ord = 0; ord < coef_order; ord++) {
        log2_var_ratio += LPC_Log2(1.0 - lpcc->parcor_coef[ord] * lpcc->parcor_coef[ord]);
    }

    /* エントロピー計算 */
    /* →サンプルあたりの最小のビット数が得られる */
    (*length_per_sample_bits) = BETA_CONST_FOR_LAPLACE_DIST + 0.5f * (log2_mean_res_power + log2_var_ratio);

    /* 推定ビット数が負値の場合は、1サンプルあたり1ビットで符号化できることを期待する */
    /* 補足）このケースは入力音声パワーが非常に低い */
    if ((*length_per_sample_bits) <= 0) {
        (*length_per_sample_bits) = 1.0;
        return LPC_APIRESULT_OK;
    }

#undef BETA_CONST_FOR_LAPLACE_DIST
#undef BETA_CONST_FOR_GAUSS_DIST

    return LPC_APIRESULT_OK;
}

/* MDL（最小記述長）を計算 */
LPCApiResult LPCCalculator_CalculateMDL(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, uint32_t coef_order, double *mdl,
    LPCWindowType window_type)
{
    uint32_t k;
    double tmp;

    /* 引数チェック */
    if ((lpcc == NULL) || (data == NULL) || (mdl == NULL)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 係数計算 */
    if (LPC_CalculateCoef(lpcc, data, num_samples, coef_order, window_type, 0.0) != LPC_ERROR_OK) {
        return LPC_APIRESULT_FAILED_TO_CALCULATION;
    }

    /* 第一項の計算 */
    /* 1次の係数は0で確定だから飛ばす */
    tmp = 0.0;
    for (k = 1; k <= coef_order; k++) {
        tmp += log(1.0 - lpcc->parcor_coef[k] * lpcc->parcor_coef[k]);
    }
    tmp *= num_samples;

    /* 第二項の計算 */
    tmp += coef_order * log(num_samples);

    (*mdl) = tmp;

    return LPC_APIRESULT_OK;
}

/* LPC係数をPARCOR係数に変換 */
static LPCError LPC_ConvertLPCtoPARCORDouble(
    struct LPCCalculator *lpcc, const double *lpc_coef, uint32_t coef_order, double *parcor_coef)
{
    int32_t i, k;
    double *tmplpc_coef, *a_vec;

    /* 引数チェック */
    if ((lpcc == NULL) || (lpc_coef == NULL) || (parcor_coef == NULL)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    assert(coef_order <= lpcc->max_order);

    /* 作業領域を割り当て */
    tmplpc_coef = lpcc->work_buffer;
    a_vec = lpcc->a_vecs[0];

    memcpy(tmplpc_coef, lpc_coef, sizeof(double) * coef_order);

    /* PARCOR係数に変換 */
    for (i = (int32_t)(coef_order - 1); i >= 0; i--) {
        const double gamma = tmplpc_coef[i];
        assert(fabs(gamma) < 1.0);
        parcor_coef[i] = -gamma;
        for (k = 0; k < i; k++) {
            a_vec[k] = tmplpc_coef[k];
        }
        for (k = 0; k < i; k++) {
            tmplpc_coef[k] = (a_vec[k] - gamma * a_vec[i - k - 1]) / (1.0 - gamma * gamma);
        }
    }

    return LPC_ERROR_OK;
}

/* LPC係数をPARCOR係数に変換して量子化 */
LPCApiResult LPC_QuantizeCoefficientsAsPARCOR(
    struct LPCCalculator *lpcc,
    const double *lpc_coef, uint32_t coef_order, uint32_t nbits_precision, int32_t *int_coef)
{
    uint32_t ord;
    int32_t qtmp;
    const int32_t qmax = (1 << (nbits_precision - 1));

    /* 引数チェック */
    if ((lpcc == NULL) || (lpc_coef == NULL)
        || (int_coef == NULL) || (nbits_precision == 0)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    if (coef_order > lpcc->max_order) {
        return LPC_APIRESULT_EXCEED_MAX_ORDER;
    }

    /* PARCOR係数に変換 */
    if (LPC_ConvertLPCtoPARCORDouble(lpcc, lpc_coef, coef_order, lpcc->parcor_coef) != LPC_ERROR_OK) {
        return LPC_APIRESULT_NG;
    }

    /* PARCOR係数を量子化して出力 */
    for (ord = 0; ord < coef_order; ord++) {
        assert(fabs(lpcc->parcor_coef[ord]) < 1.0);
        qtmp = (int32_t)LPC_Round(lpcc->parcor_coef[ord] * pow(2.0, nbits_precision - 1));
        /* 正負境界の丸め込み */
        if (qtmp >= qmax) {
            qtmp = qmax - 1;
        } else if (qtmp < -qmax) {
            qtmp = -qmax;
        }
        int_coef[ord] = qtmp;
    }

    return LPC_APIRESULT_OK;
}

/* LPC係数の整数量子化 */
LPCApiResult LPC_QuantizeCoefficients(
    const double *double_coef, uint32_t coef_order, uint32_t nbits_precision, uint32_t max_bits,
    int32_t *int_coef, uint32_t *coef_rshift)
{
    uint32_t rshift;
    int32_t ord, ndigit, qtmp;
    double max, qerror;
    const int32_t qmax = (1 << (nbits_precision - 1));

    /* 引数チェック */
    if ((double_coef == NULL) || (int_coef == NULL)
            || (coef_rshift == NULL) || (nbits_precision == 0)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* 係数絶対値の計算 */
    max = 0.0;
    for (ord = 0; ord < (int32_t)coef_order; ord++) {
        if (max < fabs(double_coef[ord])) {
            max = fabs(double_coef[ord]);
        }
    }

    /* 与えられたビット数で表現できないほど小さいときは0とみなす */
    if (max <= pow(2.0, -(int32_t)(nbits_precision - 1))) {
        (*coef_rshift) = nbits_precision;
        memset(int_coef, 0, sizeof(int32_t) * coef_order);
        return LPC_APIRESULT_OK;
    }

    /* 最大値を[1/2, 1)に収めるための右シフト量の計算 */
    /* max = x * 2^ndigit, |x| in [1/2, 1)を計算 */
    (void)frexp(max, &ndigit);
    /* 符号ビットを落とす */
    nbits_precision--;
    /* nbits_precisionで表現可能にするためのシフト量計算 */
    assert((int32_t)nbits_precision >= ndigit);
    rshift = (uint32_t)((int32_t)nbits_precision - ndigit);

    /* 右シフト量が最大を越えている場合は切り捨て（係数最大値が小さい場合） */
    if (rshift >= max_bits) {
        rshift = max_bits - 1;
    }

    /* 量子化 */
    qerror = 0.0;
    for (ord = (int32_t)coef_order - 1; ord >= 0; ord--) {
        /* 前の係数の誤差を取り込んで量子化 */
        /* インパルスの先頭部分には誤差を入れたくないため、末尾から処理 */
        qerror += double_coef[ord] * pow(2.0, rshift);
        qtmp = (int32_t)LPC_Round(qerror);
        /* 正負境界の丸め込み */
        if (qtmp >= qmax) {
            qtmp = qmax - 1;
        } else if (qtmp < -qmax) {
            qtmp = -qmax;
        }
        /* 引いた分が量子化誤差として残る */
        qerror -= qtmp;
        int_coef[ord] = qtmp;
    }
    (*coef_rshift) = rshift;

    return LPC_APIRESULT_OK;
}

/* LPC係数により予測/誤差出力 */
LPCApiResult LPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift)
{
    uint32_t smpl, ord;

    /* 引数チェック */
    if ((data == NULL) || (coef == NULL)
            || (residual == NULL) || (coef_rshift == 0)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    memcpy(residual, data, sizeof(int32_t) * num_samples);

    /* LPC係数による予測 */
    for (smpl = 1; smpl < coef_order; smpl++) {
        int32_t predict = (1 << (coef_rshift - 1));
        for (ord = 0; ord < smpl; ord++) {
            predict += (coef[ord] * data[smpl - ord - 1]);
        }
        residual[smpl] += (predict >> coef_rshift);
    }
    for (smpl = coef_order; smpl < num_samples; smpl++) {
        int32_t predict = (1 << (coef_rshift - 1));
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl - ord - 1]);
        }
        residual[smpl] += (predict >> coef_rshift);
    }

    return LPC_APIRESULT_OK;
}

/* LPC係数により合成(in-place) */
LPCApiResult LPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift)
{
    uint32_t smpl, ord;

    /* 引数チェック */
    if ((data == NULL) || (coef == NULL) || (coef_rshift == 0)) {
        return LPC_APIRESULT_INVALID_ARGUMENT;
    }

    /* LPC係数による予測 */
    for (smpl = 1; smpl < coef_order; smpl++) {
        int32_t predict = (1 << (coef_rshift - 1));
        for (ord = 0; ord < smpl; ord++) {
            predict += (coef[ord] * data[smpl - ord - 1]);
        }
        data[smpl] -= (predict >> coef_rshift);
    }
    for (smpl = coef_order; smpl < num_samples; smpl++) {
        int32_t predict = (1 << (coef_rshift - 1));
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl - ord - 1]);
        }
        data[smpl] -= (predict >> coef_rshift);
    }

    return LPC_APIRESULT_OK;
}
