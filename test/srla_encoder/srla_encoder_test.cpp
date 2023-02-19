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
        struct SRLAHeader *header__p            = p_header;\
        header__p->format_version               = SRLA_FORMAT_VERSION;\
        header__p->codec_version                = SRLA_CODEC_VERSION;\
        header__p->num_channels                 = 1;\
        header__p->sampling_rate                = 44100;\
        header__p->bits_per_sample              = 16;\
        header__p->num_samples                  = 1024;\
        header__p->max_num_samples_per_block    = 32;\
        header__p->preset                       = 0;\
    } while (0);

/* 有効なエンコードパラメータをセット */
#define SRLAEncoder_SetValidEncodeParameter(p_parameter)\
    do {\
        struct SRLAEncodeParameter *param__p = p_parameter;\
        param__p->num_channels              = 1;\
        param__p->bits_per_sample           = 16;\
        param__p->sampling_rate             = 44100;\
        param__p->min_num_samples_per_block = 1024;\
        param__p->max_num_samples_per_block = 2048;\
        param__p->preset                    = 0;\
    } while (0);

/* 有効なコンフィグをセット */
#define SRLAEncoder_SetValidConfig(p_config)\
    do {\
        struct SRLAEncoderConfig *config__p = p_config;\
        config__p->max_num_channels          = 8;\
        config__p->min_num_samples_per_block = 1024;\
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

        /* 異常なブロックあたり最大サンプル数 */
        SRLA_SetValidHeader(&header);
        header.max_num_samples_per_block = 0;
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
        sufficient_size = (2 * parameter.num_channels * parameter.max_num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.max_num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* 無効な引数を渡す */
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(NULL, input, parameter.max_num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, NULL, parameter.max_num_samples_per_block,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, 0,
                    data, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
                    NULL, sufficient_size, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
                    data, 0, &output_size));
        EXPECT_EQ(
                SRLA_APIRESULT_INVALID_ARGUMENT,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
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
        sufficient_size = (2 * parameter.num_channels * parameter.max_num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.max_num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.max_num_samples_per_block);
        }

        /* エンコーダ作成 */
        encoder = SRLAEncoder_Create(&config, NULL, 0);
        ASSERT_TRUE(encoder != NULL);

        /* パラメータセット前にエンコード: エラー */
        EXPECT_EQ(
                SRLA_APIRESULT_PARAMETER_NOT_SET,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
                    data, sufficient_size, &output_size));

        /* パラメータ設定 */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_SetEncodeParameter(encoder, &parameter));

        /* 1ブロックエンコード */
        EXPECT_EQ(
                SRLA_APIRESULT_OK,
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
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
        sufficient_size = (2 * parameter.num_channels * parameter.max_num_samples_per_block * parameter.bits_per_sample) / 8;

        /* データ領域確保 */
        data = (uint8_t *)malloc(sufficient_size);
        for (ch = 0; ch < parameter.num_channels; ch++) {
            input[ch] = (int32_t *)malloc(sizeof(int32_t) * parameter.max_num_samples_per_block);
            /* 無音セット */
            memset(input[ch], 0, sizeof(int32_t) * parameter.max_num_samples_per_block);
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
                SRLAEncoder_EncodeBlock(encoder, input, parameter.max_num_samples_per_block,
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

/* ダイクストラ法テスト */
TEST(SRLAEncoderTest, DijkstraTest)
{
    /* 隣接行列の重み */
    struct DijkstraTestCaseAdjacencyMatrixWeight {
        uint32_t i;
        uint32_t j;
        double weight;
    };
    
    /* ダイクストラ法のテストケース */
    typedef struct DijkstraTestCase {
        uint32_t num_nodes;
        uint32_t start_node;
        uint32_t goal_node;
        double min_cost;
        uint32_t num_weights;
        const DijkstraTestCaseAdjacencyMatrixWeight *weight;
        uint32_t len_answer_route_path;
        const uint32_t *answer_route_path;
    };
    
    /* テストケース0の重み */
    static const DijkstraTestCaseAdjacencyMatrixWeight test_case0_weight[] = {
        { 0, 1, 114514 }
    };
    /* テストケース0の回答経路 */
    static const uint32_t test_case0_answer_route[] = { 0, 1 };
    
    /* テストケース1の重み */
    static const DijkstraTestCaseAdjacencyMatrixWeight test_case1_weight[] = {
        { 0, 1, 30 }, { 0, 3, 10 }, { 0, 2, 15 },
        { 1, 3, 25 }, { 1, 4, 60 },
        { 2, 3, 40 }, { 2, 5, 20 },
        { 3, 6, 35 },
        { 4, 6, 20 },
        { 5, 6, 30 }
    };
    /* テストケース1の回答経路 */
    static const uint32_t test_case1_answer_route[] = { 0, 3, 6 };
    
    /* テストケース2の重み */
    static const DijkstraTestCaseAdjacencyMatrixWeight test_case2_weight[] = {
        {  0,  1,  15 }, {  0,  2,  58 }, {  0,  3,  79 }, {  0,  4,   1 }, {  0,  5,  44 },
        {  0,  6,  78 }, {  0,  7,  61 }, {  0,  8,  90 }, {  0,  9,  95 },
        {  1,  2,  53 }, {  1,  3,  78 }, {  1,  4,  49 }, {  1,  5,  72 }, {  1,  6,  50 },
        {  1,  7,  43 }, {  1,  8,  25 }, {  1,  9, 100 },
        {  2,  3,  51 }, {  2,  4,  70 }, {  2,  5,  59 }, {  2,  6,  31 }, {  2,  7,  71 },
        {  2,  8,  21 }, {  2,  9,  55 },
        {  3,  4,  46 }, {  3,  5,   7 }, {  3,  6,  81 }, {  3,  7,  92 }, {  3,  8,  71 },
        {  3,  9,  48 },
        {  4,  5,   7 }, {  4,  6,  18 }, {  4,  7,  11 }, {  4,  8,  36 },
        {  4,  9,  38 },
        {  5,  6,  54 }, {  5,  7,  85 }, {  5,  8,  84 }, {  5,  9,  36 }, {  5, 10,   1 },
        {  6,  7,  57 }, {  6,  8,  85 }, {  6,  9,  45 },
        {  7,  8,  28 }, {  7,  9,  93 },
        {  8,  9,  11 },
        {  9, 10,  92 },
        { 10, 11,  29 }, { 10, 12,  45 }, { 10, 13,  53 }, { 10, 14,   8 },
        { 10, 15,  16 }, { 10, 16,  41 }, { 10, 17,  51 }, { 10, 18,  95 },
        { 10, 19,  94 },
        { 11, 12,  64 }, { 11, 13,  31 }, { 11, 14,   6 }, { 11, 15,  91 },
        { 11, 16,  72 }, { 11, 17,  90 }, { 11, 18,  56 }, { 11, 19,  41 },
        { 12, 13, 100 }, { 12, 14,  68 }, { 12, 15,  48 }, { 12, 16,  73 },
        { 12, 17,  25 }, { 12, 18,  31 }, { 12, 19,  79 },
        { 13, 14,   1 }, { 13, 15,  38 }, { 13, 16,  17 }, { 13, 17,  81 },
        { 13, 18,  21 }, { 13, 19,  58 },
        { 14, 15,  47 }, { 14, 16,  35 }, { 14, 17,  36 }, { 14, 18,   3 },
        { 14, 19,  64 },
        { 15, 16,  19 }, { 15, 17,  22 }, { 15, 18,  51 }, { 15, 19,  58 },
        { 15, 20,  99 },
        { 16, 17,  11 }, { 16, 18,  68 }, { 16, 19,  86 },
        { 17, 18,  63 }, { 17, 19,  97 },
        { 18, 19,  64 },
        { 19, 20,  86 },
        { 20, 21,  40 }, { 20, 22,  28 }, { 20, 23,  59 }, { 20, 24, 14 },
        { 20, 25,  77 }, { 20, 26,  90 }, { 20, 27,  91 }, { 20, 28, 74 },
        { 21, 22,  21 }, { 21, 23,  78 }, { 21, 24,  26 }, { 21, 25, 76 },
        { 21, 26,  38 }, { 21, 27,  32 }, { 21, 28,  36 },
        { 22, 23,  12 }, { 22, 24,  18 }, { 22, 25,  68 }, { 22, 26, 40 },
        { 22, 27,  86 }, { 22, 28,  19 },
        { 23, 24,  32 }, { 23, 25,  77 }, { 23, 26,  63 }, { 23, 27, 57 },
        { 23, 28,  78 },
        { 24, 25,  33 }, { 24, 26,  81 }, { 24, 27,  58 }, { 24, 28, 3 },
        { 25, 26,  89 }, { 25, 27,  28 }, { 25, 28,  83 }, { 25, 29, 42 },
        { 26, 27,  83 }, { 26, 28,  87 },
        { 27, 28,  75 },
        { 28, 29,  85 }
    };
    /* テストケース2の回答経路 */
    static const uint32_t test_case2_answer_route[] = { 0, 4, 5, 10, 15, 20, 24, 25, 29 };
    
    /* ダイクストラ法のテストケース */
    static const DijkstraTestCase test_cases[] = {
        /* テストケース0（コーナーケース）:
         * ノード数2, 最小コスト: 114514, 経路 0 -> 1 */
        {
            2,
            0,
            1,
            114514,
            sizeof(test_case0_weight) / sizeof(test_case0_weight[0]),
            test_case0_weight,
            sizeof(test_case0_answer_route) / sizeof(test_case0_answer_route[0]),
            test_case0_answer_route
        },
        /* テストケース1:
         * ノード数7, 最小コスト: 45, 経路 0 -> 3 -> 6 */
        {
            7,
            0,
            6,
            45,
            sizeof(test_case1_weight) / sizeof(test_case1_weight[0]),
            test_case1_weight,
            sizeof(test_case1_answer_route) / sizeof(test_case1_answer_route[0]),
            test_case1_answer_route
        },
        /* テストケース2:
         * ノード数30, 最小コスト: 213, 経路 0 -> 4 -> 5 -> 10 -> 15 -> 20 -> 24 -> 25 -> 29 */
        {
            30,
            0,
            29,
            213,
            sizeof(test_case2_weight) / sizeof(test_case2_weight[0]),
            test_case2_weight,
            sizeof(test_case2_answer_route) / sizeof(test_case2_answer_route[0]),
            test_case2_answer_route
        }
    };
    /* ダイクストラ法のテストケース数 */
    const uint32_t num_test_case = sizeof(test_cases) / sizeof(test_cases[0]);
    
    /* ダイクストラ法の実行テスト */
    {
        struct SRLAOptimalBlockPartitionCalculator *obpc;
        uint32_t test_no, i, j, node, is_ok;
        double cost;
    
        /* 全テストケースに対してテスト */
        for (test_no = 0; test_no < num_test_case; test_no++) {
            const DijkstraTestCase *p_test = &test_cases[test_no];
            int32_t work_size;
            void *work;
    
            /* ノード数num_nodesでハンドルを作成 */
            work_size = SRLAOptimalBlockPartitionCalculator_CalculateWorkSize(p_test->num_nodes, 1);
            ASSERT_TRUE(work_size > 0);
            work = malloc(work_size);

            obpc = SRLAOptimalBlockPartitionCalculator_Create(p_test->num_nodes, 1, work, work_size);
            ASSERT_TRUE(obpc != NULL);
    
            /* 隣接行列をセット */
            for (i = 0; i < obpc->max_num_nodes; i++) {
                for (j = 0; j < obpc->max_num_nodes; j++) {
                    obpc->adjacency_matrix[i][j] = SRLAENCODER_DIJKSTRA_BIGWEIGHT;
                }
            }
            for (i = 0; i < p_test->num_weights; i++) {
                const DijkstraTestCaseAdjacencyMatrixWeight *p = &p_test->weight[i];
                obpc->adjacency_matrix[p->i][p->j] = p->weight;
            }
    
            /* ダイクストラ法実行 */
            ASSERT_EQ(
                SRLAOptimalBlockPartitionCalculator_ApplyDijkstraMethod(obpc,
                    p_test->num_nodes, p_test->start_node, p_test->goal_node, &cost),
                SRLA_ERROR_OK);
    
            /* コストのチェック */
            EXPECT_FLOAT_EQ(p_test->min_cost, cost);
    
            /* 経路のチェック */
            is_ok = 1;
            node = p_test->goal_node;
            for (i = 0; i < p_test->len_answer_route_path; i++) {
                if (node != p_test->answer_route_path[p_test->len_answer_route_path - i - 1]) {
                    is_ok = 0;
                    break;
                }
                node = obpc->path[node];
            }
            EXPECT_EQ(1, is_ok);

            /* ハンドル破棄 */
            SRLAOptimalBlockPartitionCalculator_Destroy(obpc);
            free(work);
        }
    }
}
