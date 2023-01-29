#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "srla_encoder.h"

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/srla_decoder/src/srla_decoder.c"
}

/* 有効なヘッダをセット */
#define SRLA_SetValidHeader(p_header)\
    do {\
        struct SRLAHeader *header__p            = p_header;\
        header__p->format_version               = SRLA_FORMAT_VERSION;\
        header__p->codec_version                = SRLA_CODEC_VERSION;\
        header__p->num_channels                 = 1;\
        header__p->sampling_rate                = 44100;\
        header__p->bits_per_sample              = 16;\
        header__p->num_samples                  = 8192;\
        header__p->max_num_samples_per_block    = 1024;\
        header__p->preset                       = 0;\
    } while (0);

/* ヘッダにある情報からエンコードパラメータを作成 */
#define SRLAEncoder_ConvertHeaderToParameter(p_header, p_parameter)\
    do {\
        const struct SRLAHeader *header__p = p_header;\
        struct SRLAEncodeParameter *param__p = p_parameter;\
        param__p->num_channels = header__p->num_channels;\
        param__p->sampling_rate = header__p->sampling_rate;\
        param__p->bits_per_sample = header__p->bits_per_sample;\
        param__p->max_num_samples_per_block = header__p->max_num_samples_per_block;\
        param__p->preset = header__p->preset;\
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

/* 有効なエンコーダコンフィグをセット */
#define SRLAEncoder_SetValidConfig(p_config)\
    do {\
        struct SRLAEncoderConfig *config__p = p_config;\
        config__p->max_num_channels          = 8;\
        config__p->max_num_samples_per_block = 4096;\
        config__p->max_num_parameters        = 32;\
    } while (0);

/* 有効なデコーダコンフィグをセット */
#define SRLADecoder_SetValidConfig(p_config)\
    do {\
        struct SRLADecoderConfig *config__p = p_config;\
        config__p->max_num_channels   = 8;\
        config__p->max_num_parameters = 32;\
        config__p->check_checksum     = 1;\
    } while (0);

/* ヘッダデコードテスト */
TEST(SRLADecoderTest, DecodeHeaderTest)
{
    /* 成功例 */
    {
        uint8_t data[SRLA_HEADER_SIZE] = { 0, };
        struct SRLAHeader header, tmp_header;

        SRLA_SetValidHeader(&header);

        /* エンコード->デコード */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeHeader(&header, data, sizeof(data)));
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &tmp_header));

        /* デコードしたヘッダの一致確認 */
        EXPECT_EQ(SRLA_FORMAT_VERSION, tmp_header.format_version);
        EXPECT_EQ(SRLA_CODEC_VERSION, tmp_header.codec_version);
        EXPECT_EQ(header.num_channels, tmp_header.num_channels);
        EXPECT_EQ(header.sampling_rate, tmp_header.sampling_rate);
        EXPECT_EQ(header.bits_per_sample, tmp_header.bits_per_sample);
        EXPECT_EQ(header.num_samples, tmp_header.num_samples);
        EXPECT_EQ(header.max_num_samples_per_block, tmp_header.max_num_samples_per_block);
        EXPECT_EQ(header.preset, tmp_header.preset);
    }

    /* ヘッダデコード失敗ケース */
    {
        struct SRLAHeader header, getheader;
        uint8_t valid_data[SRLA_HEADER_SIZE] = { 0, };
        uint8_t data[SRLA_HEADER_SIZE];

        /* 有効な内容を作っておく */
        SRLA_SetValidHeader(&header);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeHeader(&header, valid_data, sizeof(valid_data)));
        /* 有効であることを確認 */
        ASSERT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(valid_data, sizeof(valid_data), &getheader));

        /* シグネチャが不正 */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint8(&data[0], 'a');
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));

        /* 以降のテストケースでは、ヘッダは取得できるが、内部のチェック関数で失敗する */

        /* 異常なフォーマットバージョン */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[4], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[4], SRLA_FORMAT_VERSION + 1);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なエンコーダバージョン */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[8], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[8], SRLA_CODEC_VERSION + 1);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なチャンネル数 */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint16BE(&data[12], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なサンプル数 */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[14], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なサンプリングレート */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[18], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なサンプルあたりビット数 */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint16BE(&data[22], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なブロックあたり最大サンプル数 */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint32BE(&data[24], 0);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));

        /* 異常なプリセット */
        memcpy(data, valid_data, sizeof(valid_data));
        memset(&getheader, 0xCD, sizeof(getheader));
        ByteArray_WriteUint8(&data[28], SRLA_NUM_PARAMETER_PRESETS);
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, sizeof(data), &getheader));
        EXPECT_EQ(SRLA_ERROR_INVALID_FORMAT, SRLADecoder_CheckHeaderFormat(&getheader));
    }
}

/* デコーダハンドル生成破棄テスト */
TEST(SRLADecoderTest, CreateDestroyHandleTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct SRLADecoderConfig config;

        /* 最低限構造体本体よりは大きいはず */
        SRLADecoder_SetValidConfig(&config);
        work_size = SRLADecoder_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct SRLADecoder));

        /* 不正な引数 */
        EXPECT_TRUE(SRLADecoder_CalculateWorkSize(NULL) < 0);

        /* 不正なコンフィグ */
        SRLADecoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        EXPECT_TRUE(SRLADecoder_CalculateWorkSize(&config) < 0);

        SRLADecoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        EXPECT_TRUE(SRLADecoder_CalculateWorkSize(&config) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct SRLADecoder *decoder;
        struct SRLADecoderConfig config;

        SRLADecoder_SetValidConfig(&config);
        work_size = SRLADecoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        decoder = SRLADecoder_Create(&config, work, work_size);
        ASSERT_TRUE(decoder != NULL);
        EXPECT_TRUE(decoder->work == work);
        EXPECT_FALSE(SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_ALLOCED_BY_OWN));
        EXPECT_FALSE(SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_SET_HEADER));
        EXPECT_TRUE(decoder->params_int != NULL);
        EXPECT_TRUE(decoder->params_int[0] != NULL);
        EXPECT_TRUE(decoder->rshifts != NULL);

        SRLADecoder_Destroy(decoder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct SRLADecoder *decoder;
        struct SRLADecoderConfig config;

        SRLADecoder_SetValidConfig(&config);

        decoder = SRLADecoder_Create(&config, NULL, 0);
        ASSERT_TRUE(decoder != NULL);
        EXPECT_TRUE(decoder->work != NULL);
        EXPECT_TRUE(SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_ALLOCED_BY_OWN));
        EXPECT_FALSE(SRLADECODER_GET_STATUS_FLAG(decoder, SRLADECODER_STATUS_FLAG_SET_HEADER));
        EXPECT_TRUE(decoder->params_int != NULL);
        EXPECT_TRUE(decoder->params_int[0] != NULL);
        EXPECT_TRUE(decoder->rshifts != NULL);

        SRLADecoder_Destroy(decoder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct SRLADecoder *decoder;
        struct SRLADecoderConfig config;

        SRLADecoder_SetValidConfig(&config);
        work_size = SRLADecoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        decoder = SRLADecoder_Create(NULL, work, work_size);
        EXPECT_TRUE(decoder == NULL);
        decoder = SRLADecoder_Create(&config, NULL, work_size);
        EXPECT_TRUE(decoder == NULL);
        decoder = SRLADecoder_Create(&config, work, 0);
        EXPECT_TRUE(decoder == NULL);

        /* ワークサイズ不足 */
        decoder = SRLADecoder_Create(&config, work, work_size - 1);
        EXPECT_TRUE(decoder == NULL);

        /* コンフィグが不正 */
        SRLADecoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        decoder = SRLADecoder_Create(&config, work, work_size);
        EXPECT_TRUE(decoder == NULL);

        SRLADecoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        decoder = SRLADecoder_Create(&config, work, work_size);
        EXPECT_TRUE(decoder == NULL);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct SRLADecoder *decoder;
        struct SRLADecoderConfig config;

        SRLADecoder_SetValidConfig(&config);

        /* 引数が不正 */
        decoder = SRLADecoder_Create(NULL, NULL, 0);
        EXPECT_TRUE(decoder == NULL);

        /* コンフィグが不正 */
        SRLADecoder_SetValidConfig(&config);
        config.max_num_channels = 0;
        decoder = SRLADecoder_Create(&config, NULL, 0);
        EXPECT_TRUE(decoder == NULL);

        SRLADecoder_SetValidConfig(&config);
        config.max_num_parameters = 0;
        decoder = SRLADecoder_Create(&config, NULL, 0);
        EXPECT_TRUE(decoder == NULL);
    }
}

/* 1ブロックデコードテスト */
TEST(SRLADecoderTest, DecodeBlockTest)
{
    /* ヘッダ設定前にデコードしてエラー */
    {
        struct SRLADecoder *decoder;
        struct SRLADecoderConfig config;
        struct SRLAHeader header;
        uint8_t *data;
        int32_t *output[SRLA_MAX_NUM_CHANNELS];
        uint32_t ch, sufficient_size, output_size, out_num_samples;

        SRLA_SetValidHeader(&header);
        SRLADecoder_SetValidConfig(&config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * header.num_channels * header.max_num_samples_per_block * header.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < header.num_channels; ch++) {
            output[ch] = (int32_t *)malloc(sizeof(int32_t) * header.max_num_samples_per_block);
        }

        /* デコーダ作成 */
        decoder = SRLADecoder_Create(&config, NULL, 0);
        ASSERT_TRUE(decoder != NULL);

        /* ヘッダセット前にデコーダしようとする */
        EXPECT_EQ(SRLA_APIRESULT_PARAMETER_NOT_SET,
                SRLADecoder_DecodeBlock(decoder, data, sufficient_size, output, header.num_channels, header.max_num_samples_per_block, &output_size, &out_num_samples));

        /* ヘッダをセット */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_SetHeader(decoder, &header));

        /* 領域の開放 */
        for (ch = 0; ch < header.num_channels; ch++) {
            free(output[ch]);
        }
        free(data);
        SRLADecoder_Destroy(decoder);
    }

    /* 無音データをエンコードデコードしてみる */
    {
        struct SRLAEncoder *encoder;
        struct SRLADecoder *decoder;
        struct SRLAEncoderConfig encoder_config;
        struct SRLADecoderConfig decoder_config;
        struct SRLAEncodeParameter parameter;
        struct SRLAHeader header, tmp_header;
        uint8_t *data;
        int32_t *input[SRLA_MAX_NUM_CHANNELS];
        int32_t *output[SRLA_MAX_NUM_CHANNELS];
        uint32_t ch, sufficient_size, output_size, decode_output_size, out_num_samples;

        SRLA_SetValidHeader(&header);
        SRLAEncoder_SetValidConfig(&encoder_config);
        SRLADecoder_SetValidConfig(&decoder_config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * header.num_channels * header.max_num_samples_per_block * header.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < header.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * header.max_num_samples_per_block);
            output[ch] = (int32_t *)malloc(sizeof(int32_t) * header.max_num_samples_per_block);
        }

        /* エンコーダデコーダ作成 */
        encoder = SRLAEncoder_Create(&encoder_config, NULL, 0);
        decoder = SRLADecoder_Create(&decoder_config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);
        ASSERT_TRUE(decoder != NULL);

        /* 入力に無音セット */
        for (ch = 0; ch < header.num_channels; ch++) {
            memset(input[ch], 0, sizeof(int32_t) * header.max_num_samples_per_block);
        }

        /* ヘッダを元にパラメータを設定 */
        SRLAEncoder_ConvertHeaderToParameter(&header, &parameter);

        /* 入力データをエンコード */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_SetEncodeParameter(encoder, &parameter));
        EXPECT_EQ(SRLA_APIRESULT_OK,
                SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));

        /* エンコードデータを簡易チェック */
        EXPECT_TRUE(output_size > SRLA_HEADER_SIZE);
        EXPECT_TRUE(output_size < sufficient_size);

        /* デコード */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, output_size, &tmp_header));
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_SetHeader(decoder, &tmp_header));
        EXPECT_EQ(SRLA_APIRESULT_OK,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));

        /* 出力チェック */
        EXPECT_EQ(output_size - SRLA_HEADER_SIZE, decode_output_size);
        EXPECT_EQ(header.max_num_samples_per_block, out_num_samples);
        for (ch = 0; ch < header.num_channels; ch++) {
            EXPECT_EQ(0, memcmp(input[ch], output[ch], sizeof(int32_t) * header.max_num_samples_per_block));
        }

        /* 領域の開放 */
        for (ch = 0; ch < header.num_channels; ch++) {
            free(output[ch]);
            free(input[ch]);
        }
        free(data);
        SRLADecoder_Destroy(decoder);
        SRLAEncoder_Destroy(encoder);
    }

    /* デコード失敗テスト */
    {
        struct SRLAEncoder *encoder;
        struct SRLADecoder *decoder;
        struct SRLAEncoderConfig encoder_config;
        struct SRLADecoderConfig decoder_config;
        struct SRLAEncodeParameter parameter;
        struct SRLAHeader header, tmp_header;
        uint8_t *data;
        int32_t *input[SRLA_MAX_NUM_CHANNELS];
        int32_t *output[SRLA_MAX_NUM_CHANNELS];
        uint32_t ch, sufficient_size, output_size, decode_output_size, out_num_samples;

        SRLA_SetValidHeader(&header);
        SRLAEncoder_SetValidConfig(&encoder_config);
        SRLADecoder_SetValidConfig(&decoder_config);

        /* 十分なデータサイズ */
        sufficient_size = (2 * header.num_channels * header.max_num_samples_per_block * header.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < header.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * header.max_num_samples_per_block);
            output[ch] = (int32_t *)malloc(sizeof(int32_t) * header.max_num_samples_per_block);
        }

        /* エンコーダデコーダ作成 */
        encoder = SRLAEncoder_Create(&encoder_config, NULL, 0);
        decoder = SRLADecoder_Create(&decoder_config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);
        ASSERT_TRUE(decoder != NULL);

        /* 入力に無音セット */
        for (ch = 0; ch < header.num_channels; ch++) {
            memset(input[ch], 0, sizeof(int32_t) * header.max_num_samples_per_block);
        }

        /* ヘッダを元にパラメータを設定 */
        SRLAEncoder_ConvertHeaderToParameter(&header, &parameter);

        /* 入力データをエンコード */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_SetEncodeParameter(encoder, &parameter));
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));

        /* エンコードデータを簡易チェック */
        EXPECT_TRUE(output_size > SRLA_HEADER_SIZE);
        EXPECT_TRUE(output_size < sufficient_size);

        /* ヘッダデコード */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_DecodeHeader(data, output_size, &tmp_header));
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLADecoder_SetHeader(decoder, &tmp_header));

        /* 不正な引数 */
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLADecoder_DecodeBlock(NULL, data, output_size, output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLADecoder_DecodeBlock(decoder, NULL, output_size, output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLADecoder_DecodeBlock(decoder, data, output_size, NULL, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLADecoder_DecodeBlock(decoder, data, output_size, output, header.num_channels, tmp_header.max_num_samples_per_block, NULL, &out_num_samples));
        EXPECT_EQ(SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLADecoder_DecodeBlock(decoder, data, output_size, output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, NULL));

        /* データサイズ不足 */
        EXPECT_EQ(SRLA_APIRESULT_INSUFFICIENT_DATA,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE - 1,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));

        /* データを一部破壊した場合にエラーを返すか */

        /* 同期コード（データ先頭16bit）破壊 */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));
        data[SRLA_HEADER_SIZE] ^= 0xFF;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));
        data[SRLA_HEADER_SIZE + 1] ^= 0xFF;
        EXPECT_EQ(SRLA_APIRESULT_INVALID_FORMAT,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        /* ブロックデータタイプ不正: データ破損検知 */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));
        data[SRLA_HEADER_SIZE + 8] = 0xC0;
        EXPECT_EQ(SRLA_APIRESULT_DETECT_DATA_CORRUPTION,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        /* ブロックチャンネルあたりサンプル数不正: データ破損検知 */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));
        data[SRLA_HEADER_SIZE + 9] ^= 0xFF;
        EXPECT_EQ(SRLA_APIRESULT_DETECT_DATA_CORRUPTION,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));
        /* データの末尾1byteがビット反転: データ破損検知 */
        EXPECT_EQ(SRLA_APIRESULT_OK, SRLAEncoder_EncodeWhole(encoder, input, header.max_num_samples_per_block, data, sufficient_size, &output_size));
        data[output_size - 1] ^= 0xEF; /* note: Fletcherは0xFFのビット反転の破損を検知できない（mod 255の都合上） */
        EXPECT_EQ(SRLA_APIRESULT_DETECT_DATA_CORRUPTION,
                SRLADecoder_DecodeBlock(decoder, data + SRLA_HEADER_SIZE, output_size - SRLA_HEADER_SIZE,
                    output, header.num_channels, tmp_header.max_num_samples_per_block, &decode_output_size, &out_num_samples));

        /* 領域の開放 */
        for (ch = 0; ch < header.num_channels; ch++) {
            free(output[ch]);
            free(input[ch]);
        }
        free(data);
        SRLADecoder_Destroy(decoder);
        SRLAEncoder_Destroy(encoder);
    }
}
