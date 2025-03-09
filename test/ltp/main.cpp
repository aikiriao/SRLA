#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/ltp/src/ltp.c"
}

/* ハンドル作成破棄テスト */
TEST(LTPCalculatorTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct LTPCalculatorConfig config;

        /* 最低限構造体本体よりは大きいはず */
        config.max_order = 1;
        config.max_num_samples = 2;
        config.max_pitch_period = 1;
        work_size = LTPCalculator_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > (int32_t)sizeof(struct LTPCalculator));

        /* 不正なコンフィグ */
        {
            struct LTPCalculatorConfig tmp_config = config;
            tmp_config.max_order = 0;
            EXPECT_TRUE(LTPCalculator_CalculateWorkSize(&tmp_config) < 0);
            tmp_config = config;
            tmp_config.max_num_samples = 0;
            EXPECT_TRUE(LTPCalculator_CalculateWorkSize(&tmp_config) < 0);
            tmp_config = config;
            tmp_config.max_pitch_period = 0;
            EXPECT_TRUE(LTPCalculator_CalculateWorkSize(&tmp_config) < 0);
            tmp_config = config;
            tmp_config.max_pitch_period = config.max_num_samples;
            EXPECT_TRUE(LTPCalculator_CalculateWorkSize(&tmp_config) < 0);
        }
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct LTPCalculatorConfig config;
        struct LTPCalculator *ltpc;

        config.max_order = 1;
        config.max_num_samples = 2;
        config.max_pitch_period = 1;
        work_size = LTPCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        ltpc = LTPCalculator_Create(&config, work, work_size);
        ASSERT_TRUE(ltpc != NULL);
        EXPECT_TRUE(ltpc->work == work);
        EXPECT_EQ(ltpc->alloced_by_own, 0);

        LTPCalculator_Destroy(ltpc);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct LTPCalculator *ltpc;
        struct LTPCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 2;
        config.max_pitch_period = 1;
        ltpc = LTPCalculator_Create(&config, NULL, 0);
        ASSERT_TRUE(ltpc != NULL);
        EXPECT_TRUE(ltpc->work != NULL);
        EXPECT_EQ(ltpc->alloced_by_own, 1);

        LTPCalculator_Destroy(ltpc);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct LTPCalculator *ltpc;
        struct LTPCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 2;
        config.max_pitch_period = 1;
        work_size = LTPCalculator_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        ltpc = LTPCalculator_Create(NULL, work, work_size);
        EXPECT_TRUE(ltpc == NULL);
        ltpc = LTPCalculator_Create(&config, NULL, work_size);
        EXPECT_TRUE(ltpc == NULL);
        ltpc = LTPCalculator_Create(&config, work, 0);
        EXPECT_TRUE(ltpc == NULL);

        /* コンフィグパラメータが不正 */
        {
            struct LTPCalculatorConfig tmp_config = config;
            tmp_config.max_num_samples = 0;
            ltpc = LTPCalculator_Create(&tmp_config, work, work_size);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_num_samples = 0;
            ltpc = LTPCalculator_Create(&tmp_config, work, work_size);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_pitch_period = 0;
            ltpc = LTPCalculator_Create(&tmp_config, work, work_size);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_pitch_period = config.max_num_samples;
            ltpc = LTPCalculator_Create(&tmp_config, work, work_size);
            EXPECT_TRUE(ltpc == NULL);
        }

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct LTPCalculator *ltpc;
        struct LTPCalculatorConfig config;

        config.max_order = 1;
        config.max_num_samples = 2;
        config.max_pitch_period = 1;

        /* コンフィグパラメータが不正 */
        {
            struct LTPCalculatorConfig tmp_config = config;
            tmp_config.max_num_samples = 0;
            ltpc = LTPCalculator_Create(&tmp_config, NULL, 0);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_num_samples = 0;
            ltpc = LTPCalculator_Create(&tmp_config, NULL, 0);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_pitch_period = 0;
            ltpc = LTPCalculator_Create(&tmp_config, NULL, 0);
            EXPECT_TRUE(ltpc == NULL);
            tmp_config = config;
            tmp_config.max_pitch_period = config.max_num_samples;
            ltpc = LTPCalculator_Create(&tmp_config, NULL, 0);
            EXPECT_TRUE(ltpc == NULL);
        }
    }
}

/* 指定された周期の正弦波を生成 */
static void LTPCalculatorTest_GenerateSin(double *data, uint32_t num_samples, uint32_t pitch_period)
{
    uint32_t smpl;
    assert(num_samples > pitch_period);
    assert(pitch_period > 1);

    for (smpl = 0; smpl < num_samples; smpl++) {
        data[smpl] = sin(2.0 * LTP_PI * smpl / pitch_period);
    }
}

/* 簡易な係数計算テスト */
TEST(LTPCalculatorTest, CalculateLTPCoefficientsTest)
{
    /* 意図したピッチが得られるかテスト（正弦波） */
    {
#define NUM_SAMPLES 2048
#define NUM_COEFS 3
#define MAX_PERIOD 200
        struct LTPCalculator* ltpc;
        struct LTPCalculatorConfig config;
        double coef[NUM_COEFS] = { 0.0, };
        double *data = NULL;
        int32_t test_pitch, ref_pitch;
        LTPApiResult ret;

        data = (double *)malloc(sizeof(double) * NUM_SAMPLES);

        config.max_num_samples = NUM_SAMPLES;
        config.max_order = NUM_COEFS;
        config.max_pitch_period = MAX_PERIOD;
        ltpc = LTPCalculator_Create(&config, NULL, 0);

        for (ref_pitch = 10; ref_pitch < config.max_pitch_period; ref_pitch += 10) {
            LTPCalculatorTest_GenerateSin(data, NUM_SAMPLES, ref_pitch);
            ret = LTPCalculator_CalculateLTPCoefficients(
                ltpc, data, NUM_SAMPLES, coef, NUM_COEFS, &test_pitch, LTP_WINDOWTYPE_WELCH, 1e-5);
            ASSERT_EQ(LTP_APIRESULT_OK, ret);
            EXPECT_EQ(ref_pitch, test_pitch);
        }

        LTPCalculator_Destroy(ltpc);

        free(data);
#undef MAX_PERIOD
#undef NUM_SAMPLES
#undef NUM_COEFS
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
