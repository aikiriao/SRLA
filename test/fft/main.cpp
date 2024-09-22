#include <stdlib.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/fft/src/fft.c"
}

/*!
* @brief DFT
* @param[in] n DFT点数
* @param[in] flag -1:順方向変換, 1:逆方向変換
* @param[in] input フーリエ変換する系列(入出力 2nサイズ必須, 偶数番目に実数部, 奇数番目に虚数部)
* @param[out] output 出力配列(xと同一サイズ)
* @note 正規化は行いません
*/
static void FFTTest_FloatDFT(int n, int flag, const double *input, double *output)
{
    int32_t i, k;
    const struct FFTComplex *in = (struct FFTComplex *)input;
    struct FFTComplex *out = (struct FFTComplex *)output;

    assert(input != NULL);
    assert(output != NULL);
    assert((flag == 1) || (flag == -1));

    for (k = 0; k < n; k++) {
        struct FFTComplex tmp = { 0.0, 0.0 };
        for (i = 0; i < n; i++) {
            const double theta = 2.0 * FFT_PI * i * k / n;
            const struct FFTComplex w = { cos(theta), flag * sin(theta) };
            tmp = FFTComplex_Add(tmp, FFTComplex_Mul(in[i], w));
        }
        out[k] = tmp;
    }
}

/* Fastルーチンの結果一致テスト */
TEST(FFTTest, CheckWithDFTTest)
{
    /* FFT */
    {
#define NUM_SAMPLES 32
#define FLOAT_EPSILON 1e-8

        int32_t i, is_ok;
        double input[NUM_SAMPLES], ref_output[NUM_SAMPLES];
        double work[NUM_SAMPLES], output[NUM_SAMPLES];

        /* インパルス */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        input[0] = 1.0;

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 直流 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            input[2 * i] = 1.0;
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 乱数 */
        srand(0);
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 2.0 * ((double)rand() / RAND_MAX - 0.5);
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
    }

    /* IFFT */
    {
#define NUM_SAMPLES 32
#define FLOAT_EPSILON 1e-8

        int32_t i, is_ok;
        double input[NUM_SAMPLES], ref_output[NUM_SAMPLES];
        double work[NUM_SAMPLES], output[NUM_SAMPLES];

        /* インパルス */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        input[0] = 1.0;

        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, 1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 直流 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 1.0;
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, 1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 乱数 */
        srand(0);
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 2.0 * ((double)rand() / RAND_MAX - 0.5);
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, input, ref_output);
        memcpy(output, input, sizeof(double) * NUM_SAMPLES);
        FFT_FloatFFT(NUM_SAMPLES / 2, 1, output, work);

        is_ok = 1;
        for (i = 0; i < NUM_SAMPLES; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
    }

    /* Real FFT */
    {
#define NUM_SAMPLES 32
#define FLOAT_EPSILON 1e-8
        int32_t i, is_ok;
        double input[NUM_SAMPLES], ref_output[NUM_SAMPLES];
        double work[NUM_SAMPLES / 2], output[NUM_SAMPLES / 2];

        /* インパルス */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = 0.0;
        }
        output[0] = input[0] = 1.0;

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        /* 直流成分の比較 */
        if (fabs(ref_output[0] - output[0]) > FLOAT_EPSILON) {
            is_ok = 0;
        }
        /* それ以外の成分の比較 */
        for (i = 2; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 直流 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = input[2 * i] = 1.0;
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        /* 直流成分の比較 */
        if (fabs(ref_output[0] - output[0]) > FLOAT_EPSILON) {
            is_ok = 0;
        }
        /* それ以外の成分の比較 */
        for (i = 2; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 乱数 */
        srand(0);
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = input[2 * i] = 2.0 * ((double)rand() / RAND_MAX - 0.5);
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, -1, output, work);

        is_ok = 1;
        /* 直流成分の比較 */
        if (fabs(ref_output[0] - output[0]) > FLOAT_EPSILON) {
            is_ok = 0;
        }
        /* それ以外の成分の比較 */
        for (i = 2; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
    }

    /* Real IFFT */
    {
#define NUM_SAMPLES 32
#define FLOAT_EPSILON 1e-8
        int32_t i, is_ok;
        double input[NUM_SAMPLES], ref_output[NUM_SAMPLES];
        double work[NUM_SAMPLES / 2], output[NUM_SAMPLES / 2];

        /* インパルス */
        /* 実信号を一旦変換 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        input[0] = 1.0;
        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);

        memcpy(input, ref_output, sizeof(double) * NUM_SAMPLES);
        output[0] = input[0];
        output[1] = 1.0; /* インパルスはナイキスト周波数成分も等しく出る */
        memcpy(&output[2], &input[2], sizeof(double) * ((NUM_SAMPLES / 2) - 2));

        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, 1, output, work);
        /* 正規化定数を合わせる */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] *= 2;
        }

        is_ok = 1;
        /* 一致確認 */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[2 * i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 直流 */
        /* 実信号を一旦変換 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES; i += 2) {
            input[i] = 1.0;
        }
        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);

        memcpy(input, ref_output, sizeof(double) * NUM_SAMPLES);
        output[0] = input[0];
        output[1] = 0.0;
        memcpy(&output[2], &input[2], sizeof(double) * ((NUM_SAMPLES / 2) - 2));

        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, 1, output, work);
        /* 正規化定数を合わせる */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] *= 2;
        }

        is_ok = 1;
        /* 一致確認 */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(ref_output[2 * i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        /* 乱数 */
        for (i = 0; i < NUM_SAMPLES; i++) {
            input[i] = 0.0;
        }
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = 0.0;
        }
        srand(0);
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] = input[2 * i] = 2.0 * ((double)rand() / RAND_MAX - 0.5);
        }

        FFTTest_FloatDFT(NUM_SAMPLES / 2, -1, input, ref_output);
        FFT_RealFFT(NUM_SAMPLES / 2, -1, output, work);

        /* IFFTして元に戻るか見る */
        FFTTest_FloatDFT(NUM_SAMPLES / 2, 1, ref_output, input);
        FFT_RealFFT(NUM_SAMPLES / 2, 1, output, work);
        /* 正規化定数を合わせる */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            output[i] *= 2;
        }

        is_ok = 1;
        /* 一致確認 */
        for (i = 0; i < NUM_SAMPLES / 2; i++) {
            if (fabs(input[2 * i] - output[i]) > FLOAT_EPSILON) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
