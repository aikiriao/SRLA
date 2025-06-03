#include "srla_utility.h"

#include <math.h>
#include <stdlib.h>
#include "srla_internal.h"

/* NLZ計算のためのテーブル */
#define UNUSED 99
static const uint32_t st_nlz10_table[64] = {
        32,     20,     19, UNUSED, UNUSED,     18, UNUSED,      7,
        10,     17, UNUSED, UNUSED,     14, UNUSED,      6, UNUSED,
    UNUSED,      9, UNUSED,     16, UNUSED, UNUSED,      1,     26,
    UNUSED,     13, UNUSED, UNUSED,     24,      5, UNUSED, UNUSED,
    UNUSED,     21, UNUSED,      8,     11, UNUSED,     15, UNUSED,
    UNUSED, UNUSED, UNUSED,      2,     27,      0,     25, UNUSED,
        22, UNUSED,     12, UNUSED, UNUSED,      3,     28, UNUSED,
        23, UNUSED,      4,     29, UNUSED, UNUSED,     30,     31
};
#undef UNUSED

/* round関数(C89で用意されていない) */
double SRLAUtility_Round(double d)
{
    return (d >= 0.0) ? floor(d + 0.5) : -floor(-d + 0.5);
}

/* log2関数（C89で定義されていない） */
double SRLAUtility_Log2(double d)
{
#define INV_LOGE2 (1.4426950408889634)  /* 1 / log(2) */
    return log(d) * INV_LOGE2;
#undef INV_LOGE2
}

/* フレッチャーのチェックサム計算 */
uint16_t SRLAUtility_CalculateFletcher16CheckSum(const uint8_t *data, size_t data_size)
{
#define MAX_BLOCK_SIZE 5802 /* c1のmodが変化しないブロックサイズ */
#define MOD255(x) (((x) + ((x) / 255)) & 0xFF) /* 255の剰余計算 */
    uint32_t c0, c1;

    /* 引数チェック */
    SRLA_ASSERT(data != NULL);

    c0 = c1 = 0;
    while (data_size > 0) {
        size_t block_size = SRLAUTILITY_MIN(MAX_BLOCK_SIZE, data_size);
        data_size -= block_size;
        while (block_size--) {
            c0 += *data++;
            c1 += c0;
        }
        c0 = MOD255(c0);
        c1 = MOD255(c1);
    }

    return (uint16_t)((c1 << 8) | c0);
#undef MOD255
#undef MAX_BLOCK_SIZE
}

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
uint32_t SRLAUtility_NLZSoft(uint32_t x)
{
    /* ハッカーのたのしみ参照 */
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x & ~(x >> 16);
    x = (x << 9) - x;
    x = (x << 11) - x;
    x = (x << 14) - x;
    return st_nlz10_table[x >> 26];
}

/* 2の冪乗数に切り上げる */
uint32_t SRLAUtility_RoundUp2PoweredSoft(uint32_t val)
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

/* LR -> MS (in-place) */
void SRLAUtility_LRtoMSConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[1][smpl] -= buffer[0][smpl];
        buffer[0][smpl] += (buffer[1][smpl] >> 1);
    }
}

/* MS -> LR (in-place) */
void SRLAUtility_MStoLRConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[0][smpl] -= (buffer[1][smpl] >> 1);
        buffer[1][smpl] += buffer[0][smpl];
    }
}

/* LR -> LS (in-place) */
void SRLAUtility_LRtoLSConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[1][smpl] -= buffer[0][smpl];
    }
}

/* LS -> LR (in-place) */
void SRLAUtility_LStoLRConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[1][smpl] += buffer[0][smpl];
    }
}

/* LR -> SR (in-place) */
void SRLAUtility_LRtoSRConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[0][smpl] = buffer[1][smpl] - buffer[0][smpl];
    }
}

/* SR -> LR (in-place) */
void SRLAUtility_SRtoLRConversion(int32_t **buffer, uint32_t num_samples)
{
    uint32_t smpl;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(buffer[0] != NULL);
    SRLA_ASSERT(buffer[1] != NULL);

    for (smpl = 0; smpl < num_samples; smpl++) {
        buffer[0][smpl] = buffer[1][smpl] - buffer[0][smpl];
    }
}

/* オフセットされている左シフト量を計算 */
uint32_t SRLAUtility_ComputeOffsetLeftShift(
    const int32_t* const* input, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t ch, smpl, offset_shift;
    uint32_t mask = 0;

    SRLA_ASSERT(input != NULL);

    /* 使用されているビットを検査 */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            mask |= (uint32_t)input[ch][smpl];
        }
    }

    /* 全入力が0の場合はシフトなしとする */
    if (mask == 0) {
        return 0;
    }

    /* ntz（末尾へ続く0の個数）を計算
     * ntz(x) = 32 - nlz(~x & (x-1)), nlz(x) = 31 - log2ceil(x)を使用 */
    offset_shift = 1 + SRLAUTILITY_LOG2FLOOR(~mask & (mask - 1));
    SRLA_ASSERT(offset_shift <= 31);

    return offset_shift;
}


/* プリエンファシスフィルタ初期化 */
void SRLAPreemphasisFilter_Initialize(struct SRLAPreemphasisFilter *preem)
{
    SRLA_ASSERT(preem != NULL);
    preem->prev = 0;
    preem->coef = 0;
}

/* 多段プリエンファシスの係数計算 */
void SRLAPreemphasisFilter_CalculateMultiStageCoefficients(
    struct SRLAPreemphasisFilter *preem, uint32_t num_preem, const int32_t *buffer, uint32_t num_samples)
{
    uint32_t i;
    double r0, r1, r2;
    double curr, succ;
    double double_coef[SRLA_NUM_PREEMPHASIS_FILTERS];

    /* 注意）現段階では2回を前提 */
    SRLA_STATIC_ASSERT(SRLA_NUM_PREEMPHASIS_FILTERS == 2);
    SRLA_ASSERT(num_preem == 2);

    SRLA_ASSERT(preem != NULL);
    SRLA_ASSERT(buffer != NULL);

    /* 相関の計算 */
    curr = buffer[0];
    succ = buffer[1];
    r0 = r1 = r2 = 0.0;
    for (i = 0; i < num_samples - 2; i++) {
        const double succsucc = buffer[i + 2];
        r0 += curr * curr;
        r1 += curr * succ;
        r2 += curr * succsucc;
        curr = succ;
        succ = succsucc;
    }
    /* i = num_samples - 1 */
    r0 += curr * curr;
    r1 += curr * succ;
    curr = succ;
    r0 += curr * curr;
    SRLA_ASSERT((r0 >= r1) && (r0 >= r2));

    /* 分散が小さい場合は全て0を設定 */
    if (r0 < 1e-6) {
        for (i = 0; i < SRLA_NUM_PREEMPHASIS_FILTERS; i++) {
            preem[i].coef = 0;
        }
        return;
    }

    /* 分散(=0次相関)で正規化 */
    r1 /= r0;
    r2 /= r0;
    r0 = 1.0;

    /* プリエンファシスの係数計算 */
    {
        /* 平方根の2乗 */
        const double sqroot = r1 * r1 * (r0 - r2) * (r0 - r2) - 4.0 * (r0 * r0 - r1 * r1) * (r1 * r1 - r0 * r2);
        if (sqroot >= 0.0) {
            double det;
            double tmpcoef[2] = { 0.0, 0.0 };
            tmpcoef[1] = (r1 * (r0 - r2) - sqrt(sqroot)) / (2.0 * (r0 * r0 - r1 * r1));
            tmpcoef[0] = (tmpcoef[1] * r1 - r2) / (tmpcoef[1] * r0 - r1);
            /* ヘッセ行列の行列式 */
            det = 4.0 * (tmpcoef[0] * tmpcoef[0] * r0 - 2.0 * tmpcoef[0] * r1 + r0) * (tmpcoef[1] * tmpcoef[1] * r0 - 2.0 * tmpcoef[1] * r1 + r0);
            det -= 4.0 * pow(2.0 * tmpcoef[0] * tmpcoef[1] * r0 - 2.0 * tmpcoef[0] * r1 - 2.0 * tmpcoef[1] * r1 + r0 + r2, 2.0);
            if (det > 0.0) {
                double_coef[0] = tmpcoef[0];
                double_coef[1] = tmpcoef[1];
            } else {
                double_coef[0] = r1;
                double_coef[1] = r1 * (r1 * r1 - r2) / (1.0 - r1 * r1);
            }
        } else {
            /* 複素数解の場合は従来通りの係数（1段ごとに分散最小）を設定 */
            double_coef[0] = r1;
            double_coef[1] = r1 * (r1 * r1 - r2) / (1.0 - r1 * r1);
        }
    }

    /* 固定小数化 */
    for (i = 0; i < SRLA_NUM_PREEMPHASIS_FILTERS; i++) {
        int32_t coef = (int32_t)SRLAUtility_Round(double_coef[i] * pow(2.0f, SRLA_PREEMPHASIS_COEF_SHIFT));
        /* 丸め込み */
        coef = SRLAUTILITY_INNER_VALUE(coef, -(1 << SRLA_PREEMPHASIS_COEF_SHIFT), (1 << SRLA_PREEMPHASIS_COEF_SHIFT) - 1);
        preem[i].coef = coef;
    }
}

/* プリエンファシス */
void SRLAPreemphasisFilter_Preemphasis(
        struct SRLAPreemphasisFilter *preem, int32_t *buffer, uint32_t num_samples)
{
    uint32_t smpl;
    int32_t prev, tmp;

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(preem != NULL);

    prev = preem->prev;
    for (smpl = 0; smpl < num_samples; smpl++) {
        tmp = buffer[smpl];
        buffer[smpl] -= (prev * preem->coef) >> SRLA_PREEMPHASIS_COEF_SHIFT;
        prev = tmp;
    }
    preem->prev = prev;
}

/* デエンファシスを複数回適用 */
void SRLAPreemphasisFilter_MultiStageDeemphasis(
    struct SRLAPreemphasisFilter *preem, uint32_t num_preem, int32_t *buffer, uint32_t num_samples)
{
    uint32_t smpl;
    const int32_t c0 = preem[0].coef;
    const int32_t c1 = preem[1].coef;

    /* 注意）現段階では2回を前提 */
    SRLA_STATIC_ASSERT(SRLA_NUM_PREEMPHASIS_FILTERS == 2);
    SRLA_ASSERT(num_preem == 2);

    SRLA_ASSERT(buffer != NULL);
    SRLA_ASSERT(preem != NULL);

    buffer[0] += (preem[1].prev * c1) >> SRLA_PREEMPHASIS_COEF_SHIFT;
    buffer[1] += (buffer[0] * c1) >> SRLA_PREEMPHASIS_COEF_SHIFT;
    buffer[0] += (preem[0].prev * c0) >> SRLA_PREEMPHASIS_COEF_SHIFT;

    for (smpl = 2; smpl < num_samples; smpl++) {
        buffer[smpl] += (buffer[smpl - 1] * c1) >> SRLA_PREEMPHASIS_COEF_SHIFT;
        buffer[smpl - 1] += (buffer[smpl - 2] * c0) >> SRLA_PREEMPHASIS_COEF_SHIFT;
    }

    preem[0].prev = buffer[num_samples - 1];
    buffer[num_samples - 1] += (buffer[num_samples - 2] * c0) >> SRLA_PREEMPHASIS_COEF_SHIFT;
    preem[1].prev = buffer[num_samples - 1];
}
