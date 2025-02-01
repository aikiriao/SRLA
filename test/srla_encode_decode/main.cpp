#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gtest/gtest.h>

#include "srla_encoder.h"
#include "srla_decoder.h"
#include "srla_utility.h"
#include "srla_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 様々な波形がエンコード -> デコードが元に戻るかを確認するテスト */

/* 波形生成関数 */
typedef void (*GenerateWaveFunction)(double **data, uint32_t num_channels, uint32_t num_samples);

/* テストケース */
struct EncodeDecodeTestCase {
    struct SRLAEncodeParameter encode_parameter; /* エンコードパラメータ */
    uint32_t offset_lshift; /* オフセット分の左シフト */
    uint32_t num_samples; /* サンプル数 */
    GenerateWaveFunction gen_wave_func; /* 波形生成関数 */
};

/* 無音の生成 */
static void SRLAEncodeDecodeTest_GenerateSilence(double **data, uint32_t num_channels, uint32_t num_samples);
/* サイン波の生成 */
static void SRLAEncodeDecodeTest_GenerateSinWave(double **data, uint32_t num_channels, uint32_t num_samples);
/* サイン波（チャンネルごとに逆相）の部 */
static void SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave(double **data, uint32_t num_channels, uint32_t num_samples);
/* 白色雑音の生成 */
static void SRLAEncodeDecodeTest_GenerateWhiteNoise(double **data, uint32_t num_channels, uint32_t num_samples);
/* チャープ信号の生成 */
static void SRLAEncodeDecodeTest_GenerateChirp(double **data, uint32_t num_channels, uint32_t num_samples);
/* 正定数信号の生成 */
static void SRLAEncodeDecodeTest_GeneratePositiveConstant(double **data, uint32_t num_channels, uint32_t num_samples);
/* 負定数信号の生成 */
static void SRLAEncodeDecodeTest_GenerateNegativeConstant(double **data, uint32_t num_channels, uint32_t num_samples);
/* ナイキスト周期の振動の生成 */
static void SRLAEncodeDecodeTest_GenerateNyquistOsc(double **data, uint32_t num_channels, uint32_t num_samples);
/* ガウス雑音の生成 */
static void SRLAEncodeDecodeTest_GenerateGaussNoise(double **data, uint32_t num_channels, uint32_t num_samples);
/* 先頭部分で微小なインパルスの生成 */
static void SRLAEncodeDecodeTest_GenerateMiniImpulse(double** data, uint32_t num_channels, uint32_t num_samples);

/* 無音の生成 */
static void SRLAEncodeDecodeTest_GenerateSilence(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = 0.0f;
        }
    }
}

/* サイン波の生成 */
static void SRLAEncodeDecodeTest_GenerateSinWave(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = sin(440.0f * 2 * M_PI * smpl / 44100.0f);
        }
    }
}

/* サイン波（チャンネルごとに逆相）の部 */
static void SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = pow(-1, ch) * sin(440.0f * 2 * M_PI * smpl / 44100.0f);
        }
    }
}

/* 白色雑音の生成 */
static void SRLAEncodeDecodeTest_GenerateWhiteNoise(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = 2.0f * ((double)rand() / RAND_MAX - 0.5f);
        }
    }
}

/* チャープ信号の生成 */
static void SRLAEncodeDecodeTest_GenerateChirp(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;
    double period;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            period = num_samples - smpl;
            data[ch][smpl] = sin((2.0f * M_PI * smpl) / period);
        }
    }
}

/* 正定数信号の生成 */
static void SRLAEncodeDecodeTest_GeneratePositiveConstant(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = 1.0f;
        }
    }
}

/* 負定数信号の生成 */
static void SRLAEncodeDecodeTest_GenerateNegativeConstant(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = -1.0f;
        }
    }
}

/* ナイキスト周期の振動の生成 */
static void SRLAEncodeDecodeTest_GenerateNyquistOsc(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = (smpl % 2 == 0) ? 1.0f : -1.0f;
        }
    }
}

/* ガウス雑音の生成 */
static void SRLAEncodeDecodeTest_GenerateGaussNoise(
        double **data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;
    double   x, y;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            /* ボックス-ミューラー法 */
            x = (double)rand() / RAND_MAX;
            y = (double)rand() / RAND_MAX;
            /* 分散は0.1f */
            data[ch][smpl] = 0.25f * sqrt(-2.0f * log(x)) * cos(2.0f * M_PI * y);
            data[ch][smpl] = (data[ch][smpl] >= 1.0f) ?   1.0f : data[ch][smpl];
            data[ch][smpl] = (data[ch][smpl] <= -1.0f) ? -1.0f : data[ch][smpl];
        }
    }
}

/* 先頭部分で微小なインパルスの生成 */
static void SRLAEncodeDecodeTest_GenerateMiniImpulse(
    double** data, uint32_t num_channels, uint32_t num_samples)
{
    uint32_t smpl, ch;

    assert(data != NULL);

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            data[ch][smpl] = 0.0f;
        }
        data[ch][1] = pow(2.0, -15.0);
    }
}

/* double入力データの固定小数化 */
static void SRLAEncodeDecodeTest_InputDoubleToInputFixedFloat(
        const struct SRLAEncodeParameter *param, uint32_t offset_lshift,
        double **input_double, uint32_t num_channels, uint32_t num_samples, int32_t **input_int32)
{
    uint32_t ch, smpl;

    assert((input_double != NULL) && (input_int32 != NULL));

    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            assert(fabs(input_double[ch][smpl]) <= 1.0f);
            /* まずはビット幅のデータを作る */
            input_int32[ch][smpl]
                = (int32_t)SRLAUtility_Round(input_double[ch][smpl] * pow(2, param->bits_per_sample - 1));
            /* クリップ */
            if (input_int32[ch][smpl] >= (1L << (param->bits_per_sample - 1))) {
                input_int32[ch][smpl] = (1L << (param->bits_per_sample - 1)) - 1;
            }
            /* 左シフト量だけ下位ビットのデータを消す */
            input_int32[ch][smpl] &= ~((1UL << offset_lshift) - 1);
        }
    }
}

/* 単一のテストケースを実行 */
static int32_t SRLAEncodeDecodeTest_ExecuteTestCase(const struct EncodeDecodeTestCase *test_case)
{
    int32_t ret;
    uint32_t smpl, ch;
    uint32_t num_samples, num_channels, data_size;
    uint32_t output_size;
    double **input_double;
    int32_t **input;
    uint8_t *data;
    int32_t **output;
    SRLAApiResult api_ret;

    struct SRLAEncoderConfig encoder_config;
    struct SRLADecoderConfig decoder_config;
    struct SRLAEncoder *encoder;
    struct SRLADecoder *decoder;
    const struct SRLAParameterPreset *preset;

    assert(test_case != NULL);
    assert(test_case->num_samples <= (1UL << 14));  /* 長過ぎる入力はNG */

    num_samples   = test_case->num_samples;
    num_channels  = test_case->encode_parameter.num_channels;
    preset = &g_srla_parameter_preset[test_case->encode_parameter.preset];
    /* 十分なデータサイズを用意（入力データPCMの2倍） */
    data_size     = SRLA_HEADER_SIZE + (2 * num_channels * num_samples * test_case->encode_parameter.bits_per_sample) / 8;

    /* エンコード・デコードコンフィグ作成 */
    /* FIXME: 仮値 */
    encoder_config.max_num_channels          = num_channels;
    encoder_config.min_num_samples_per_block = test_case->encode_parameter.min_num_samples_per_block;
    encoder_config.max_num_samples_per_block = test_case->encode_parameter.max_num_samples_per_block;
    encoder_config.max_num_lookahead_samples = test_case->encode_parameter.num_lookahead_samples;
    encoder_config.max_num_parameters        = preset->max_num_parameters;
    decoder_config.max_num_channels          = num_channels;
    decoder_config.max_num_parameters        = preset->max_num_parameters;
    decoder_config.check_checksum            = 1;

    /* 一時領域の割り当て */
    input_double  = (double **)malloc(sizeof(double*) * num_channels);
    input         = (int32_t **)malloc(sizeof(int32_t*) * num_channels);
    output        = (int32_t **)malloc(sizeof(int32_t*) * num_channels);
    data          = (uint8_t *)malloc(data_size);
    for (ch = 0; ch < num_channels; ch++) {
        input_double[ch]  = (double *)malloc(sizeof(double) * num_samples);
        input[ch]         = (int32_t *)malloc(sizeof(int32_t) * num_samples);
        output[ch]        = (int32_t *)malloc(sizeof(int32_t) * num_samples);
    }

    /* エンコード・デコードハンドル作成 */
    encoder = SRLAEncoder_Create(&encoder_config, NULL, 0);
    decoder = SRLADecoder_Create(&decoder_config, NULL, 0);
    if ((encoder == NULL) || (decoder == NULL)) {
        ret = 1;
        goto EXIT;
    }

    /* 波形生成 */
    test_case->gen_wave_func(input_double, num_channels, num_samples);

    /* 固定小数化 */
    SRLAEncodeDecodeTest_InputDoubleToInputFixedFloat(
            &test_case->encode_parameter, test_case->offset_lshift, input_double, num_channels, num_samples, input);

    /* 波形フォーマットと波形パラメータをセット */
    if ((api_ret = SRLAEncoder_SetEncodeParameter(encoder, &test_case->encode_parameter)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Failed to set encode parameter. ret:%d \n", api_ret);
        ret = 2;
        goto EXIT;
    }

    /* エンコード */
    if ((api_ret = SRLAEncoder_EncodeWhole(encoder,
                    (const int32_t **)input, num_samples, data, data_size, &output_size, 0)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Encode failed! ret:%d \n", api_ret);
        ret = 3;
        goto EXIT;
    }

    /* デコード */
    if ((api_ret = SRLADecoder_DecodeWhole(decoder, data, output_size, output, num_channels, num_samples)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Decode failed! ret:%d \n", api_ret);
        ret = 4;
        goto EXIT;
    }

    /* 一致確認 */
    for (ch = 0; ch < num_channels; ch++) {
        /* バイナリ一致していたら次へ */
        if (memcmp(input[ch], output[ch], sizeof(int32_t) * num_samples) == 0) {
            continue;
        }
        /* 間違っている箇所の確認 */
        for (smpl = 0; smpl < num_samples; smpl++) {
            if (input[ch][smpl] != output[ch][smpl]) {
                printf("%5d %12d vs %12d \n", smpl, input[ch][smpl], output[ch][smpl]);
                ret = 5;
                goto EXIT;
            }
        }
    }

    /* 可変ブロックエンコード */
    if ((api_ret = SRLAEncoder_EncodeWhole(encoder,
        (const int32_t**)input, num_samples, data, data_size, &output_size, NULL)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Variable block encode failed! ret:%d \n", api_ret);
        ret = 6;
        goto EXIT;
    }

    /* デコード */
    if ((api_ret = SRLADecoder_DecodeWhole(decoder, data, output_size, output, num_channels, num_samples)) != SRLA_APIRESULT_OK) {
        fprintf(stderr, "Decode failed! ret:%d \n", api_ret);
        ret = 7;
        goto EXIT;
    }

    /* 一致確認 */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            if (input[ch][smpl] != output[ch][smpl]) {
                printf("%5d %12d vs %12d \n", smpl, input[ch][smpl], output[ch][smpl]);
                ret = 8;
                goto EXIT;
            }
        }
    }

    /* ここまで来れば成功 */
    ret = 0;

EXIT:
    /* ハンドル開放 */
    SRLADecoder_Destroy(decoder);
    SRLAEncoder_Destroy(encoder);

    /* 一時領域の開放 */
    for (ch = 0; ch < num_channels; ch++) {
        free(input_double[ch]);
        free(input[ch]);
        free(output[ch]);
    }
    free(input_double);
    free(input);
    free(output);
    free(data);

    return ret;
}

/* インスタンス作成破棄テスト */
TEST(SRLAEncodeDecodeTest, EncodeDecodeCheckTest)
{
    int32_t   test_ret;
    uint32_t  test_no;

    /* テストケース配列 */
    static const struct EncodeDecodeTestCase test_case[] = {
        /* 無音の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 16, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 24, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2,  8, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 16, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 24, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8,  8, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 16, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 24, 8000, 512, 1024, 2048, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1,  8, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 16, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 24, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2,  8, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 16, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 24, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8,  8, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 16, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 24, 8000, 512, 1024, 1536, 0  }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSilence },
        /* サイン波の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinWave },
        /* サイン波（チャンネルごとに逆相）の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateSinChSignFlippedWave },
        /* 白色雑音の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateWhiteNoise },
        /* チャープ信号の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateChirp },
        /* 正定数信号の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GeneratePositiveConstant },
        /* 負定数信号の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNegativeConstant },
        /* ナイキスト周期振動信号の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateNyquistOsc },
        /* ガウス雑音信号の部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateGaussNoise },
        /* 微小インパルスの部 */
        { { 1,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8,  8, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 16, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 24, 8000, 512, 1024, 2048, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8,  8, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 16, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 24, 8000, 512, 1024, 1536, 0 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8,  8, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 16, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 24, 8000, 512, 1024, 2048, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 1, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 2, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8,  8, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 16, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
        { { 8, 24, 8000, 512, 1024, 1536, SRLA_NUM_PARAMETER_PRESETS - 1 }, 0, 8500, SRLAEncodeDecodeTest_GenerateMiniImpulse },
    };

    /* テストケース数 */
    const uint32_t num_test_case = sizeof(test_case) / sizeof(test_case[0]);

    /* デバッグのしやすさのため、乱数シードを固定 */
    srand(0);

    for (test_no = 0; test_no < num_test_case; test_no++) {
        test_ret = SRLAEncodeDecodeTest_ExecuteTestCase(&test_case[test_no]);
        EXPECT_EQ(0, test_ret);
        if (test_ret != 0) {
            fprintf(stderr, "Encode / Decode Test Failed at case %d. ret:%d \n", test_no, test_ret);
        }
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
