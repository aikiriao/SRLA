#ifndef LPC_H_INCLUDED
#define LPC_H_INCLUDED

#include <stdint.h>

/* API結果型 */
typedef enum LPCApiResultTag {
    LPC_APIRESULT_OK = 0,                 /* OK */
    LPC_APIRESULT_NG,                     /* 分類不能なエラー */
    LPC_APIRESULT_INVALID_ARGUMENT,       /* 不正な引数 */
    LPC_APIRESULT_EXCEED_MAX_ORDER,       /* 最大次数を超えた */
    LPC_APIRESULT_EXCEED_MAX_NUM_SAMPLES, /* 最大入力サンプル数を超えた */
    LPT_APIRESULT_FAILED_TO_FIND_PITCH,   /* ピッチ周期を見つけられなかった */
    LPC_APIRESULT_FAILED_TO_CALCULATION   /* 計算に失敗 */
} LPCApiResult;

/* 窓関数の種類 */
typedef enum LPCWindowTypeTag {
    LPC_WINDOWTYPE_RECTANGULAR = 0, /* 矩形窓 */
    LPC_WINDOWTYPE_SIN,             /* サイン窓 */
    LPC_WINDOWTYPE_WELCH            /* Welch窓 */
} LPCWindowType;

/* LPC係数計算ハンドル */
struct LPCCalculator;

/* 初期化コンフィグ */
struct LPCCalculatorConfig {
    uint32_t max_order;        /* 最大次数 */
    uint32_t max_num_samples;  /* 最大入力サンプル数 */
};

#ifdef __cplusplus
extern "C" {
#endif

/* LPC係数計算ハンドルのワークサイズ計算 */
int32_t LPCCalculator_CalculateWorkSize(const struct LPCCalculatorConfig *config);

/* LPC係数計算ハンドルの作成 */
struct LPCCalculator *LPCCalculator_Create(const struct LPCCalculatorConfig *config, void *work, int32_t work_size);

/* LPC係数計算ハンドルの破棄 */
void LPCCalculator_Destroy(struct LPCCalculator *lpcc);

/* Levinson-Durbin再帰計算によりLPC係数を求める */
LPCApiResult LPCCalculator_CalculateLPCCoefficients(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    LPCWindowType window_type, double regular_term);

/* Levinson-Durbin再帰計算により与えられた次数まで全てのLPC係数を求める（倍精度） */
/* error_varsは0次の誤差分散（分散）からmax_coef_order次の分散まで求めるためerror_varsのサイズはmax_coef_order+1要する */
LPCApiResult LPCCalculator_CalculateMultipleLPCCoefficients(
    struct LPCCalculator* lpcc,
    const double* data, uint32_t num_samples, double **lpc_coefs, double *error_vars, uint32_t max_coef_order,
    LPCWindowType window_type, double regular_term);

/* 補助関数法よりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsAF(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    uint32_t max_num_iteration, LPCWindowType window_type, double regular_term);

/* Burg法によりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsBurg(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order);

/* SVRよりLPC係数を求める（倍精度） */
LPCApiResult LPCCalculator_CalculateLPCCoefficientsSVR(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    uint32_t max_num_iteration, LPCWindowType window_type,
    double regular_term, const double *margin_list, uint32_t margin_list_size);

/* 入力データからサンプルあたりの推定符号長を求める */
LPCApiResult LPCCalculator_EstimateCodeLength(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, uint32_t bits_per_sample,
    uint32_t coef_order, double *length_per_sample_bits, LPCWindowType window_type);

/* MDL（最小記述長）を計算 */
LPCApiResult LPCCalculator_CalculateMDL(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples, uint32_t coef_order, double *mdl,
    LPCWindowType window_type);

/* LPC係数をPARCOR係数に変換して量子化する */
LPCApiResult LPC_QuantizeCoefficientsAsPARCOR(
    struct LPCCalculator *lpcc,
    const double *lpc_coef, uint32_t coef_order, uint32_t nbits_precision, int32_t *int_coef);

/* LPC係数の整数量子化 */
LPCApiResult LPC_QuantizeCoefficients(
    const double *double_coef, uint32_t coef_order, uint32_t nbits_precision, uint32_t max_bits,
    int32_t *int_coef, uint32_t *coef_rshift);

/* LPC係数により予測/誤差出力 */
LPCApiResult LPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift);

/* LPC係数により合成(in-place) */
LPCApiResult LPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift);

/* LTP係数とピッチ周期を計算 */
LPCApiResult LPCCalculator_CalculateLTPCoefficients(
    struct LPCCalculator *lpcc,
    const double *data, uint32_t num_samples,
    int32_t min_pitch_period, int32_t max_pitch_period,
    double *coef, uint32_t coef_order, int32_t *pitch_period,
    LPCWindowType window_type, double regular_term);

#ifdef __cplusplus
}
#endif

#endif /* LPC_H_INCLUDED */
