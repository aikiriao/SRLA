#ifndef SRLAUTILITY_H_INCLUDED
#define SRLAUTILITY_H_INCLUDED

#include "srla_stdint.h"
#include <stddef.h>

/* 未使用引数警告回避 */
#define SRLAUTILITY_UNUSED_ARGUMENT(arg)  ((void)(arg))
/* 算術右シフト */
#if ((((int32_t)-1) >> 1) == ((int32_t)-1))
/* 算術右シフトが有効な環境では、そのまま右シフト */
#define SRLAUTILITY_SHIFT_RIGHT_ARITHMETIC(sint32, rshift) ((sint32) >> (rshift))
#else
/* 算術右シフトが無効な環境では、自分で定義する ハッカーのたのしみのより引用 */
/* 注意）有効範囲:0 <= rshift <= 32 */
#define SRLAUTILITY_SHIFT_RIGHT_ARITHMETIC(sint32, rshift) ((((uint64_t)(sint32) + 0x80000000UL) >> (rshift)) - (0x80000000UL >> (rshift)))
#endif
/* 符号関数 ハッカーのたのしみより引用 補足）val==0の時は0を返す */
#define SRLAUTILITY_SIGN(val)  (((val) > 0) - ((val) < 0))
/* nの倍数への切り上げ */
#define SRLAUTILITY_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 最大値の取得 */
#define SRLAUTILITY_MAX(a,b) (((a) > (b)) ? (a) : (b))
/* 最小値の取得 */
#define SRLAUTILITY_MIN(a,b) (((a) < (b)) ? (a) : (b))
/* 最小値以上最小値以下に制限 */
#define SRLAUTILITY_INNER_VALUE(val, min, max) (SRLAUTILITY_MIN((max), SRLAUTILITY_MAX((min), (val))))
/* 2の冪乗か？ */
#define SRLAUTILITY_IS_POWERED_OF_2(val) (!((val) & ((val) - 1)))
/* 符号付き32bit数値を符号なし32bit数値に一意変換 */
#define SRLAUTILITY_SINT32_TO_UINT32(sint) (((int32_t)(sint) < 0) ? ((uint32_t)((-((sint) << 1)) - 1)) : ((uint32_t)(((sint) << 1))))
/* 符号なし32bit数値を符号付き32bit数値に一意変換 */
#define SRLAUTILITY_UINT32_TO_SINT32(uint) ((int32_t)((uint) >> 1) ^ -(int32_t)((uint) & 1))
/* 絶対値の取得 */
#define SRLAUTILITY_ABS(val)               (((val) > 0) ? (val) : -(val))

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
#if defined(__GNUC__)
/* ビルトイン関数を使用 */
#define SRLAUTILITY_NLZ(x) (((x) > 0) ? (uint32_t)__builtin_clz(x) : 32U)
#elif defined(_MSC_VER)
/* ビルトイン関数を使用 */
__inline uint32_t SRLAUTILITY_NLZ(uint32_t x)
{
    unsigned long result;
    return (_BitScanReverse(&result, x) != 0) ? (31U - result) : 32U;
}
#else
/* ソフトウェア実装を使用 */
#define SRLAUTILITY_NLZ(x) SRLAUtility_NLZSoft(x)
#endif

/* ceil(log2(val))の計算 */
#define SRLAUTILITY_LOG2CEIL(x) (32U - SRLAUTILITY_NLZ((uint32_t)((x) - 1U)))
/* floor(log2(val))の計算 */
#define SRLAUTILITY_LOG2FLOOR(x) (31U - SRLAUTILITY_NLZ(x))

/* 2の冪乗数(1,2,4,8,16,...)への切り上げ */
#if defined(__GNUC__) || defined(_MSC_VER)
/* ビルトイン関数を使用 */
#define SRLAUTILITY_ROUNDUP2POWERED(x) (1U << SRLAUTILITY_LOG2CEIL(x))
#else
/* ソフトウェア実装を使用 */
#define SRLAUTILITY_ROUNDUP2POWERED(x) SRLAUtility_RoundUp2PoweredSoft(x)
#endif

/* 2次元配列の領域ワークサイズ計算 */
#define SRLA_CALCULATE_2DIMARRAY_WORKSIZE(type, size1, size2)\
    ((size1) * ((int32_t)sizeof(type *) + SRLA_MEMORY_ALIGNMENT\
        + (size2) * (int32_t)sizeof(type) + SRLA_MEMORY_ALIGNMENT))

/* 2次元配列の領域割当て */
#define SRLA_ALLOCATE_2DIMARRAY(ptr, work_ptr, type, size1, size2)\
    do {\
        uint32_t i;\
        (work_ptr) = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);\
        (ptr) = (type **)work_ptr;\
        (work_ptr) += sizeof(type *) * (size1);\
        for (i = 0; i < (size1); i++) {\
            (work_ptr) = (uint8_t *)SRLAUTILITY_ROUNDUP((uintptr_t)work_ptr, SRLA_MEMORY_ALIGNMENT);\
            (ptr)[i] = (type *)work_ptr;\
            (work_ptr) += sizeof(type) * (size2);\
        }\
    } while (0)

/* プリエンファシス/デエンファシスフィルタ */
struct SRLAPreemphasisFilter {
    int32_t prev;
    int32_t coef;
};

#ifdef __cplusplus
extern "C" {
#endif

/* round関数(C89で用意されていない) */
double SRLAUtility_Round(double d);

/* log2関数（C89で定義されていない） */
double SRLAUtility_Log2(double d);

/* フレッチャーのチェックサム計算 */
uint16_t SRLAUtility_CalculateFletcher16CheckSum(const uint8_t *data, size_t data_size);

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
uint32_t SRLAUtility_NLZSoft(uint32_t val);

/* 2の冪乗に切り上げる */
uint32_t SRLAUtility_RoundUp2PoweredSoft(uint32_t val);

/* LR -> MS (in-place) */
void SRLAUtility_LRtoMSConversion(int32_t **buffer, uint32_t num_samples);

/* MS -> LR (in-place) */
void SRLAUtility_MStoLRConversion(int32_t **buffer, uint32_t num_samples);

/* LR -> LS (in-place) */
void SRLAUtility_LRtoLSConversion(int32_t **buffer, uint32_t num_samples);

/* LS -> LR (in-place) */
void SRLAUtility_LStoLRConversion(int32_t **buffer, uint32_t num_samples);

/* LR -> RS (in-place) */
void SRLAUtility_LRtoRSConversion(int32_t **buffer, uint32_t num_samples);

/* RS -> LR (in-place) */
void SRLAUtility_RStoLRConversion(int32_t **buffer, uint32_t num_samples);

/* プリエンファシスフィルタ初期化 */
void SRLAPreemphasisFilter_Initialize(struct SRLAPreemphasisFilter *preem);

/* プリエンファシスフィルタ係数計算 */
void SRLAPreemphasisFilter_CalculateCoefficient(
        struct SRLAPreemphasisFilter *preem, const int32_t *buffer, uint32_t num_samples);

/* プリエンファシス */
void SRLAPreemphasisFilter_Preemphasis(
        struct SRLAPreemphasisFilter *preem, int32_t *buffer, uint32_t num_samples);

/* デエンファシスを複数回適用 */
void SRLAPreemphasisFilter_MultiStageDeemphasis(
    struct SRLAPreemphasisFilter *preem, uint32_t num_preem, int32_t *buffer, uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* SRLAUTILITY_H_INCLUDED */
