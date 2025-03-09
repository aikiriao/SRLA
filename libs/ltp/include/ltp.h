#ifndef LTP_H_INCLUDED
#define LTP_H_INCLUDED

#include <stdint.h>

/* API結果型 */
typedef enum LTPApiResultTag {
    LTP_APIRESULT_OK = 0,                 /* OK */
    LTP_APIRESULT_NG,                     /* 分類不能なエラー */
    LTP_APIRESULT_INVALID_ARGUMENT,       /* 不正な引数 */
    LTP_APIRESULT_INVALID_ORDER,          /* 不正な次数 */
    LTP_APIRESULT_EXCEED_MAX_ORDER,       /* 最大次数を超えた */
    LTP_APIRESULT_EXCEED_MAX_NUM_SAMPLES, /* 最大入力サンプル数を超えた */
    LTP_APIRESULT_FAILED_TO_FIND_PITCH,   /* ピッチが見つからなかった */
    LTP_APIRESULT_FAILED_TO_CALCULATION   /* 計算に失敗 */
} LTPApiResult;

/* 窓関数の種類 */
typedef enum LTPWindowTypeTag {
    LTP_WINDOWTYPE_RECTANGULAR = 0, /* 矩形窓 */
    LTP_WINDOWTYPE_SIN,             /* サイン窓 */
    LTP_WINDOWTYPE_WELCH            /* Welch窓 */
} LTPWindowType;

/* LTP係数計算ハンドル */
struct LTPCalculator;

/* 初期化コンフィグ */
struct LTPCalculatorConfig {
    uint32_t max_order;        /* 最大次数（奇数限定） */
    uint32_t max_num_samples;  /* 最大入力サンプル数 */
    uint32_t max_pitch_period; /* 最大ピッチ周期 */
};

#ifdef __cplusplus
extern "C" {
#endif

/* LTP係数計算ハンドルのワークサイズ計算 */
int32_t LTPCalculator_CalculateWorkSize(const struct LTPCalculatorConfig *config);

/* LTP係数計算ハンドルの作成 */
struct LTPCalculator *LTPCalculator_Create(const struct LTPCalculatorConfig *config, void *work, int32_t work_size);

/* LTP係数計算ハンドルの破棄 */
void LTPCalculator_Destroy(struct LTPCalculator *ltpc);

/* LTP係数を求める */
LTPApiResult LTPCalculator_CalculateLTPCoefficients(
    struct LTPCalculator *ltpc,
    const double *data, uint32_t num_samples, double *coef, uint32_t coef_order,
    int32_t *pitch_period, LTPWindowType window_type, double regular_term);

#ifdef __cplusplus
}
#endif

#endif /* LTP_H_INCLUDED */
