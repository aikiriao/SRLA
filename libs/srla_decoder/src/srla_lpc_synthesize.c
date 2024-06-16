#include "srla_lpc_synthesize.h"

#include <string.h>
#include "srla_internal.h"
#include "srla_utility.h"

/* SSE4.1 / AVX2を使うか
#define USE_SSE41
#define USE_AVX2
*/

/* LPC係数により合成(in-place) */
#if defined(USE_SSE41)
#ifdef _MSC_VER
#include <intrin.h>
#define DECLALIGN(x) __declspec(align(x))
#else
#include <x86intrin.h>
#define DECLALIGN(x) __attribute__((aligned(x)))
#endif
void SRLALPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift)
{
    int32_t smpl, ord;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */
    int32_t predict;

    /* 引数チェック */
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(coef != NULL);

    smpl = coef_order;

    if (coef_order >= 4) {
        uint32_t i;
        __m128i vcoef[SRLA_MAX_COEFFICIENT_ORDER];
        /* 係数をベクトル化 */
        for (i = 0; i < coef_order; i++) {
            vcoef[i] = _mm_set1_epi32(coef[i]);
        }
        for (; smpl < num_samples - coef_order - 4; smpl += 4) {
            /* 4サンプル並列に処理
            int32_t predict[4] = { half, half, half, half }
            for (ord = 0; ord < coef_order - 3; ord++) {
                predict[0] += (coef[ord] * data[smpl - coef_order + ord + 0]);
                predict[1] += (coef[ord] * data[smpl - coef_order + ord + 1]);
                predict[2] += (coef[ord] * data[smpl - coef_order + ord + 2]);
                predict[3] += (coef[ord] * data[smpl - coef_order + ord + 3]);
            }
            */
            DECLALIGN(16) int32_t predict[4];
            __m128i vdata;
            __m128i vpred = _mm_set1_epi32(half);
            for (ord = 0; ord < (int32_t)coef_order - 3 - 4; ord += 4) {
                const int32_t *dat = &data[smpl - coef_order + ord];
                vdata = _mm_loadu_epi32(&dat[0]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 0], vdata));
                vdata = _mm_loadu_epi32(&dat[1]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 1], vdata));
                vdata = _mm_loadu_epi32(&dat[2]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 2], vdata));
                vdata = _mm_loadu_epi32(&dat[3]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 3], vdata));
            }
            for (; ord < coef_order - 3; ord++) {
                vdata = _mm_loadu_epi32(&data[smpl - coef_order + ord]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord], vdata));
            }
            _mm_store_si128(&predict, vpred);

            /* ord = coef_order - 3 */
            /* data[smpl + 0] .. data[smpl + 2]に依存関係があるため処理
            * TODO: ここもSSEでやり切ってdataに結果を直接storeしたい
            predict[0] += (coef[ord + 0] * data[smpl - 3 + 0]);
            predict[0] += (coef[ord + 1] * data[smpl - 3 + 1]);
            predict[0] += (coef[ord + 2] * data[smpl - 3 + 2]);
            data[smpl + 0] -= (predict[0] >> coef_rshift);
            predict[1] += (coef[ord + 0] * data[smpl - 3 + 1]);
            predict[1] += (coef[ord + 1] * data[smpl - 3 + 2]);
            predict[1] += (coef[ord + 2] * data[smpl - 3 + 3]);
            data[smpl + 1] -= (predict[1] >> coef_rshift);
            predict[2] += (coef[ord + 0] * data[smpl - 3 + 2]);
            predict[2] += (coef[ord + 1] * data[smpl - 3 + 3]);
            predict[2] += (coef[ord + 2] * data[smpl - 3 + 4]);
            data[smpl + 2] -= (predict[2] >> coef_rshift);
            predict[3] += (coef[ord + 0] * data[smpl - 3 + 3]);
            predict[3] += (coef[ord + 1] * data[smpl - 3 + 4]);
            predict[3] += (coef[ord + 2] * data[smpl - 3 + 5]);
            data[smpl + 3] -= (predict[3] >> coef_rshift);
            */
            for (i = 0; i < 4; i++) {
                predict[i] += (coef[ord + 0] * data[smpl - 3 + i + 0]);
                predict[i] += (coef[ord + 1] * data[smpl - 3 + i + 1]);
                predict[i] += (coef[ord + 2] * data[smpl - 3 + i + 2]);
                data[smpl + i] -= (predict[i] >> coef_rshift);
            }
        }
    }

    /* 余ったサンプル分の処理 */
    for (; smpl < num_samples; smpl++) {
        int32_t predict = half;
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl - coef_order + ord]);
        }
        data[smpl] -= (predict >> coef_rshift);
    }
}
#elif defined(USE_AVX2)
#ifdef _MSC_VER
#include <immintrin.h>
#define DECLALIGN(x) __declspec(align(x))
#else
#include <x86intrin.h>
#define DECLALIGN(x) __attribute__((aligned(x)))
#endif
void SRLALPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift)
{
    int32_t smpl, ord;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */

    /* 引数チェック */
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(coef != NULL);

    smpl = coef_order;

    if (coef_order >= 8) {
        uint32_t i;
        __m256i vcoef[SRLA_MAX_COEFFICIENT_ORDER];
        /* 係数をベクトル化 */
        for (i = 0; i < coef_order; i++) {
            vcoef[i] = _mm256_set1_epi32(coef[i]);
        }
        for (; smpl < num_samples - coef_order - 8; smpl += 8) {
            /* 8サンプル並列に処理 */
            DECLALIGN(16) int32_t predict[8];
            __m256i vdata;
            __m256i vpred = _mm256_set1_epi32(half);
            for (ord = 0; ord < (int32_t)coef_order - 7 - 8; ord += 8) {
                const int32_t *dat = &data[smpl - coef_order + ord];
                vdata = _mm256_loadu_epi32(&dat[0]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 0], vdata));
                vdata = _mm256_loadu_epi32(&dat[1]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 1], vdata));
                vdata = _mm256_loadu_epi32(&dat[2]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 2], vdata));
                vdata = _mm256_loadu_epi32(&dat[3]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 3], vdata));
                vdata = _mm256_loadu_epi32(&dat[4]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 4], vdata));
                vdata = _mm256_loadu_epi32(&dat[5]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 5], vdata));
                vdata = _mm256_loadu_epi32(&dat[6]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 6], vdata));
                vdata = _mm256_loadu_epi32(&dat[7]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord + 7], vdata));
            }
            for (; ord < coef_order - 7; ord++) {
                vdata = _mm256_loadu_epi32(&data[smpl - coef_order + ord]);
                vpred = _mm256_add_epi32(vpred, _mm256_mullo_epi32(vcoef[ord], vdata));
            }
            _mm256_store_si256(&predict, vpred);

            /* ord = coef_order - 7 */
            for (i = 0; i < 8; i++) {
                predict[i] += (coef[ord + 0] * data[smpl - 7 + i + 0]);
                predict[i] += (coef[ord + 1] * data[smpl - 7 + i + 1]);
                predict[i] += (coef[ord + 2] * data[smpl - 7 + i + 2]);
                predict[i] += (coef[ord + 3] * data[smpl - 7 + i + 3]);
                predict[i] += (coef[ord + 4] * data[smpl - 7 + i + 4]);
                predict[i] += (coef[ord + 5] * data[smpl - 7 + i + 5]);
                predict[i] += (coef[ord + 6] * data[smpl - 7 + i + 6]);
                data[smpl + i] -= (predict[i] >> coef_rshift);
            }
        }
    } else if (coef_order >= 4) {
        uint32_t i;
        __m128i vcoef[SRLA_MAX_COEFFICIENT_ORDER];
        /* 係数をベクトル化 */
        for (i = 0; i < coef_order; i++) {
            vcoef[i] = _mm_set1_epi32(coef[i]);
        }
        for (; smpl < num_samples - coef_order - 4; smpl += 4) {
            /* 4サンプル並列に処理 */
            DECLALIGN(16) int32_t predict[4];
            __m128i vdata;
            __m128i vpred = _mm_set1_epi32(half);
            for (ord = 0; ord < (int32_t)coef_order - 3 - 4; ord += 4) {
                const int32_t *dat = &data[smpl - coef_order + ord];
                vdata = _mm_loadu_epi32(&dat[0]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 0], vdata));
                vdata = _mm_loadu_epi32(&dat[1]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 1], vdata));
                vdata = _mm_loadu_epi32(&dat[2]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 2], vdata));
                vdata = _mm_loadu_epi32(&dat[3]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord + 3], vdata));
            }
            for (; ord < coef_order - 3; ord++) {
                vdata = _mm_loadu_epi32(&data[smpl - coef_order + ord]);
                vpred = _mm_add_epi32(vpred, _mm_mullo_epi32(vcoef[ord], vdata));
            }
            _mm_store_si128(&predict, vpred);

            /* ord = coef_order - 3 */
            for (i = 0; i < 4; i++) {
                predict[i] += (coef[ord + 0] * data[smpl - 3 + i + 0]);
                predict[i] += (coef[ord + 1] * data[smpl - 3 + i + 1]);
                predict[i] += (coef[ord + 2] * data[smpl - 3 + i + 2]);
                data[smpl + i] -= (predict[i] >> coef_rshift);
            }
        }
    }

    /* 余ったサンプル分の処理 */
    for (; smpl < num_samples; smpl++) {
        int32_t predict = half;
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl - coef_order + ord]);
        }
        data[smpl] -= (predict >> coef_rshift);
    }
}
#else
void SRLALPC_Synthesize(
    int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift)
{
    uint32_t smpl, ord;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */
    int32_t predict;

    /* 引数チェック */
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(coef != NULL);

    for (smpl = 0; smpl < num_samples - coef_order; smpl++) {
        predict = half;
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl + ord]);
        }
        data[smpl + ord] -= (predict >> coef_rshift);
    }
}
#endif
