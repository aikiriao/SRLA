#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/srla_encoder/src/srla_encoder.c"
}

/* 有効なヘッダをセット */
#define SRLA_SetValidHeader(p_header)\
    do {\
        struct SRLAHeader *header__p       = p_header;\
        header__p->format_version           = SRLA_FORMAT_VERSION;\
        header__p->codec_version            = SRLA_CODEC_VERSION;\
        header__p->num_channels             = 1;\
        header__p->sampling_rate            = 44100;\
        header__p->bits_per_sample          = 16;\
        header__p->num_samples              = 1024;\
        header__p->num_samples_per_block    = 32;\
        header__p->preset                   = 0;\
    } while (0);

/* 有効なエンコードパラメータをセット */
#define SRLAEncoder_SetValidEncodeParameter(p_parameter)\
    do {\
        struct SRLAEncodeParameter *param__p = p_parameter;\
        param__p->num_channels          = 1;\
        param__p->bits_per_sample       = 16;\
        param__p->sampling_rate         = 44100;\
        param__p->num_samples_per_block = 1024;\
        param__p->preset                = 0;\
    } while (0);

/* 有効なコンフィグをセット */
#define SRLAEncoder_SetValidConfig(p_config)\
    do {\
        struct SRLAEncoderConfig *config__p = p_config;\
        config__p->max_num_channels          = 8;\
        config__p->max_num_samples_per_block = 4096;\
        config__p->max_num_parameters        = 32;\
    } while (0);

/* ヘッダエンコードテスト */
TEST(SRLAEncoderTest, EncodeHeaderTest)
{
    /* ヘッダエンコード成功ケース */
    {
        struct SRLAHeader header;
        uint8_t data[SRLA_HEADER_SIZE] = { 0, };

        SRLA_SetValidHeader(&header);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 簡易チェック */
        EXPECT_EQ('1', data[0]);
        EXPECT_EQ('2', data[1]);
        EXPECT_EQ('4', data[2]);
        EXPECT_EQ('9', data[3]);
    }

    /* ヘッダエンコード失敗ケース */
    {
        struct SRLAHeader header;
        uint8_t data[SRLA_HEADER_SIZE] = { 0, };

        /* 引数が不正 */
        SRLA_SetValidHeader(&header);
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT, SRLAEncoder_EncodeHeader(NULL, data, sizeof(data)));
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT, SRLAEncoder_EncodeHeader(&header, NULL, sizeof(data)));

        /* データサイズ不足 */
        SRLA_SetValidHeader(&header);
        EXPECT_EQ(SRLA_APIRESULT_INSUFFICIENT_BUFFER, SRLAEncoder_EncodeHeader(&header, data, sizeof(data) - 1));
        EXPECT_EQ(SRLA_APIRESULT_INSUFFICIENT_BUFFER, SRLAEncoder_EncodeHeader(&header, data, SRLA_HEADER_SIZE - 1));

        /* 異常なチャンネル数 */
        SRLA_SetValidHeader(&header);
        header.num_channels = 0;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なサンプル数 */
        SRLA_SetValidHeader(&header);
        header.num_samples = 0;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なサンプリングレート */
        SRLA_SetValidHeader(&header);
        header.sampling_rate = 0;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なビット深度 */
        SRLA_SetValidHeader(&header);
        header.bits_per_sample = 0;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なブロックあたりサンプル数 */
        SRLA_SetValidHeader(&header);
        header.num_samples_per_block = 0;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* 異常なプリセット */
        SRLA_SetValidHeader(&header);
        header.preset = SRLA_NUM_PARAMETER_PRESETS;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));
    }

}

/* エンコードハンドル作成破棄テスト */
TEST(SRLAEncoderTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct SRLAEncoderConfig config;

        /* 最低限構造体本体よりは大きいはず */
        SRLAEncoder_SetValidConfig(&config);
        work_size = SRLAEncoder_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct SRLAEncoder));

        /* 不正な引数 */
        EXPECT_TRUE(SRLAEncoder_CalculateWorkSize(NULL) < 0);

        /* 不正なコンフィグ */
        SRLAEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        EXPECT_TRUE(SRLAEncoder_CalculateWorkSize(&config) < 0);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        EXPECT_TRUE(SRLAEncoder_CalculateWorkSize(&config) < 0);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        EXPECT_TRUE(SRLAEncoder_CalculateWorkSize(&config) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;

        SRLAEncoder_SetValidConfig(&config);
        work_size = SRLAEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        encoder = SRLAEncoder_Create(&config, work, work_size);
        ASSERT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work == work);
        EXPECT_EQ(encoder->set_parameter, 0);
        EXPECT_EQ(encoder->alloced_by_own, 0);
        EXPECT_TRUE(encoder->coder != NULL);

        SRLAEncoder_Destroy(encoder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;

        SRLAEncoder_SetValidConfig(&config);

        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work != NULL);
        EXPECT_EQ(encoder->set_parameter, 0);
        EXPECT_EQ(encoder->alloced_by_own, 1);
        EXPECT_TRUE(encoder->coder != NULL);

        SRLAEncoder_Destroy(encoder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;

        SRLAEncoder_SetValidConfig(&config);
        work_size = SRLAEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        encoder = SRLAEncoder_Create(NULL, work, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = SRLAEncoder_Create(&config, NULL, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = SRLAEncoder_Create(&config, work, 0);
        EXPECT_TRUE(encoder == NULL);

        /* ワークサイズ不足 */
        encoder = SRLAEncoder_Create(&config, work, work_size - 1);
        EXPECT_TRUE(encoder == NULL);

        /* コンフィグが不正 */
        SRLAEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        encoder = SRLAEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        encoder = SRLAEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        encoder = SRLAEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;

        SRLAEncoder_SetValidConfig(&config);

        /* 引数が不正 */
        encoder = SRLAEncoder_Create(NULL, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        /* コンフィグが不正 */
        SRLAEncoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_samples_per_block = 0;
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        SRLAEncoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);
    }
}

/* 1ブロックエンコードテスト */
TEST(SRLAEncoderTest, EncodeBlockTest)
{
    /* 無効な引数 */
    {
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;
        struct SRLAEncodeParameter parameter;
        int32_t *input[SRLA_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;

        SRLAEncoder_SetValidEncodeParameter(&parameter);
        SRLAEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* 無効な引数を渡す */
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(NULL, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, NULL, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, 0,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    NULL, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, 0, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, NULL));

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        SRLAEncoder_Destroy(encoder);
    }

    /* パラメータ未セットでエンコード */
    {
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;
        struct SRLAEncodeParameter parameter;
        int32_t *input[SRLA_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;

        SRLAEncoder_SetValidEncodeParameter(&parameter);
        SRLAEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* パラメータセット前にエンコード: エラー */
        EXPECT_EQ(
                SRLA_APIRESULT_PARAMETER_NOT_SET,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* パラメータ設定 */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_SetEncodeParameter(encoder, &parameter));

        /* 1ブロックエンコード */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        SRLAEncoder_Destroy(encoder);
    }

    /* 無音エンコード */
    {
        struct SRLAEncoder *encoder;
        struct SRLAEncoderConfig config;
        struct SRLAEncodeParameter parameter;
        struct BitStream stream;
        int32_t *input[SRLA_MAX_NUM_CHANNELS];
        uint8_t *data;
        uint32_t ch, sufficient_size, output_size, num_samples;
        uint32_t bitbuf;

        SRLAEncoder_SetValidEncodeParameter(&parameter);
        SRLAEncoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * parameter.num_channels * parameter.num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* パラメータ設定 */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_SetEncodeParameter(encoder, &parameter));

        /* 1ブロックエンコード */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* ブロック先頭の同期コードがあるので2バイトよりは大きいはず */
        EXPECT_TRUE(output_size > 2);

        /* 内容の確認 */
        BitReader_Open(&stream, data, output_size);
        /* 同期コード */
        BitReader_GetBits(&stream, &bitbuf, 16);
        EXPECT_EQ(SRLA_BLOCK_SYNC_CODE, bitbuf);
        /* ブロックデータタイプ */
        BitReader_GetBits(&stream, &bitbuf, 2);
        EXPECT_TRUE((bitbuf == SRLA_BLOCK_DATA_TYPE_COMPRESSDATA)
                || (bitbuf == SRLA_BLOCK_DATA_TYPE_SILENT)
                || (bitbuf == SRLA_BLOCK_DATA_TYPE_RAWDATA));
        /* この後データがエンコードされているので、まだ終端ではないはず */
        BitStream_Tell(&stream, (int32_t *)&bitbuf);
        EXPECT_TRUE(bitbuf < output_size);
        BitStream_Close(&stream);

        /* 領域の開放 */
        for (ch = 0; ch < parameter.num_channels; ch++) {
            free(input[ch]);
        }
        free(data);
        SRLAEncoder_Destroy(encoder);
    }
}
