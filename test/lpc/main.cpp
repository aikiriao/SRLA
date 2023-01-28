#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/lpc/src/lpc.c"
}

/* ハンドル作成破棄テスト */
TEST(LPCCalculatorTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct LPCCalculatorConfig config;

        /* 最低限構造体本体よりは大きいはず */
        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct LPCCalculator));

        /* 不正なコンフィグ */
        EXPECT_TRUE(LPCCalculator_CalculateWorkSize(0) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LPCCalculatorConfig config;
        struct LPCCalculator *lpcc;

        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        lpcc = LPCCalculator_Create(&config, work, work_size);
        ASSERT_TRUE(lpcc != NULL);
        EXPECT_TRUE(lpcc->work == work);
        EXPECT_EQ(lpcc->alloced_by_own, 0);

        LPCCalculator_Destroy(lpcc);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        ASSERT_TRUE(lpcc != NULL);
        EXPECT_TRUE(lpcc->work != NULL);
        EXPECT_EQ(lpcc->alloced_by_own, 1);

        LPCCalculator_Destroy(lpcc);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 1;
        work_size = LPCCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        lpcc = LPCCalculator_Create(NULL,    work, work_size);
        EXPECT_TRUE(lpcc == NULL);
        lpcc = LPCCalculator_Create(&config, NULL, work_size);
        EXPECT_TRUE(lpcc == NULL);
        lpcc = LPCCalculator_Create(&config, work, 0);
        EXPECT_TRUE(lpcc == NULL);

        /* コンフィグパラメータが不正 */
        config.max_order = 0; config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, work, work_size);
        EXPECT_TRUE(lpcc == NULL);
        config.max_order = 1; config.max_num_samples = 0;
        lpcc = LPCCalculator_Create(&config, work, work_size);
        EXPECT_TRUE(lpcc == NULL);

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct LPCCalculator *lpcc;
        struct LPCCalculatorConfig config;

        /* コンフィグパラメータが不正 */
        config.max_order = 0; config.max_num_samples = 1;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_TRUE(lpcc == NULL);
        config.max_order = 1; config.max_num_samples = 0;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_TRUE(lpcc == NULL);
    }
}

/* （テスト用）PARCOR係数をLPC係数に変換 */
static LPCError LPC_ConvertPARCORtoLPCDouble(
    struct LPCCalculator* lpcc, const double* parcor_coef, uint32_t coef_order, double *lpc_coef)
{
    int32_t i, k;
    double *a_vec;

    /* 引数チェック */
    if ((lpcc == NULL) || (lpc_coef == NULL) || (parcor_coef == NULL)) {
        return LPC_ERROR_INVALID_ARGUMENT;
    }

    /* 次数チェック */
    assert(coef_order <= lpcc->max_order);

    /* 作業領域を割り当て */
    a_vec = lpcc->a_vecs[0];

    /* 再帰計算 */
    lpc_coef[0] = -parcor_coef[0];
    for (i = 1; i < coef_order; i++) {
        const double gamma = -parcor_coef[i];
        for (k = 0; k < i; k++) {
            a_vec[k] = lpc_coef[k];
        }
        for (k = 0; k < i; k++) {
            lpc_coef[k] = a_vec[k] + (gamma * a_vec[i - k - 1]);
        }
        lpc_coef[i] = gamma;
    }

    return LPC_ERROR_OK;
}

/* LPC係数をPARCOR係数に変換するテスト */
TEST(LPCCalculatorTest, LPC_ConvertLPCandPARCORTest)
{
    /* LPC->PARCORの簡単な成功例 */
    {
#define NUM_SAMPLES 32
#define COEF_ORDER 16
        uint32_t i;
        struct LPCCalculator* lpcc;
        struct LPCCalculatorConfig config;
        double data[NUM_SAMPLES], lpc_coef[COEF_ORDER], answer[COEF_ORDER], test[COEF_ORDER];

        for (i = 0; i < NUM_SAMPLES; i++) {
            data[i] = sin(0.1 * i);
        }

        config.max_num_samples = NUM_SAMPLES; config.max_order = COEF_ORDER;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        ASSERT_TRUE(lpcc != NULL);

        /* 係数計算 */
        ASSERT_EQ(LPC_APIRESULT_OK,
            LPCCalculator_CalculateLPCCoefficients(lpcc,
                data, NUM_SAMPLES, lpc_coef, COEF_ORDER, LPC_WINDOWTYPE_RECTANGULAR, 0.0));
        memcpy(answer, lpcc->parcor_coef, sizeof(double) * COEF_ORDER);

        LPCCalculator_Destroy(lpcc);

        /* LPC->PARCOR変換 */
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_EQ(LPC_ERROR_OK, LPC_ConvertLPCtoPARCORDouble(lpcc, lpc_coef, COEF_ORDER, test));

        /* 一致確認 */
        for (i = 0; i < COEF_ORDER; i++) {
            EXPECT_FLOAT_EQ(answer[i], test[i]);
        }

        LPCCalculator_Destroy(lpcc);
#undef NUM_SAMPLES
#undef COEF_ORDER
    }

    /* PARCOR->LPCの簡単な成功例 */
    {
#define NUM_SAMPLES 32
#define COEF_ORDER 16
        uint32_t i;
        struct LPCCalculator* lpcc;
        struct LPCCalculatorConfig config;
        double data[NUM_SAMPLES], lpc_coef[COEF_ORDER], answer[COEF_ORDER], test[COEF_ORDER];

        for (i = 0; i < NUM_SAMPLES; i++) {
            data[i] = sin(0.1 * i);
        }

        config.max_num_samples = NUM_SAMPLES; config.max_order = COEF_ORDER;
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        ASSERT_TRUE(lpcc != NULL);

        ASSERT_EQ(LPC_APIRESULT_OK,
            LPCCalculator_CalculateLPCCoefficients(lpcc,
                data, NUM_SAMPLES, lpc_coef, COEF_ORDER, LPC_WINDOWTYPE_RECTANGULAR, 0.0));
        memcpy(answer, lpcc->parcor_coef, sizeof(double) * COEF_ORDER);

        LPCCalculator_Destroy(lpcc);

        /* PARCOR->LPC変換 */
        lpcc = LPCCalculator_Create(&config, NULL, 0);
        EXPECT_EQ(LPC_ERROR_OK, LPC_ConvertPARCORtoLPCDouble(lpcc, answer, COEF_ORDER, test));

        for (i = 0; i < COEF_ORDER; i++) {
            EXPECT_FLOAT_EQ(lpc_coef[i], test[i]);
        }

        LPCCalculator_Destroy(lpcc);
#undef NUM_SAMPLES
#undef COEF_ORDER
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
