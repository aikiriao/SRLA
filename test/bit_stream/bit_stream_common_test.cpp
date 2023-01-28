#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* インスタンス作成破棄テスト */
TEST(BitStreamTest, CreateDestroyTest)
{
    /* インスタンス作成・破棄 */
    {
        struct BitStream strm;
        uint8_t test_memory[] = {'A', 'I', 'K', 'A', 'T', 'S', 'U'};
        const uint32_t test_memory_size = sizeof(test_memory) / sizeof(test_memory[0]);

        /* 書きモードでインスタンス作成 */
        BitWriter_Open(&strm, test_memory, test_memory_size);
        EXPECT_TRUE(strm.memory_image == test_memory);
        EXPECT_EQ(test_memory_size, strm.memory_size);
        EXPECT_TRUE(strm.memory_p == test_memory);
        EXPECT_EQ(0, strm.bit_buffer);
        EXPECT_EQ(32, strm.bit_count);
        EXPECT_TRUE(!(strm.flags & BITSTREAM_FLAGS_MODE_READ));
        BitStream_Close(&strm);

        /* 読みモードでインスタンス作成 */
        BitReader_Open(&strm, test_memory, test_memory_size);
        EXPECT_TRUE(strm.memory_image == test_memory);
        EXPECT_EQ(test_memory_size, strm.memory_size);
        EXPECT_TRUE(strm.memory_p == test_memory);
        EXPECT_EQ(0, strm.bit_buffer);
        EXPECT_EQ(0, strm.bit_count);
        EXPECT_TRUE(strm.flags & BITSTREAM_FLAGS_MODE_READ);
        BitStream_Close(&strm);
    }
}

/* PutBit関数テスト */
TEST(BitStreamTest, PutGetTest)
{
    {
        struct BitStream strm;
        uint8_t bit_pattern[] = { 1, 1, 1, 1, 0, 0, 0, 0 };
        uint8_t memory_image[256];
        uint32_t bit_pattern_length = sizeof(bit_pattern) / sizeof(bit_pattern[0]);
        uint32_t i, is_ok;

        /* 書き込んでみる */
        BitWriter_Open(&strm, memory_image, sizeof(memory_image));
        for (i = 0; i < bit_pattern_length; i++) {
            BitWriter_PutBits(&strm, bit_pattern[i], 1);
        }
        BitStream_Close(&strm);

        /* 正しく書き込めているか？ */
        BitReader_Open(&strm, memory_image, sizeof(memory_image));
        is_ok = 1;
        for (i = 0; i < bit_pattern_length; i++) {
            uint32_t buf;
            BitReader_GetBits(&strm, &buf, 1);
            if ((uint8_t)buf != bit_pattern[i]) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);
        BitStream_Close(&strm);
    }

    /* PutBit関数テスト2 8bitパターンチェック */
    {
        struct BitStream strm;
        uint8_t memory_image[256];
        uint32_t i, is_ok, nbits;

        for (nbits = 1; nbits <= 8; nbits++) {
            /* 書き込んでみる */
            BitWriter_Open(&strm, memory_image, sizeof(memory_image));
            for (i = 0; i < (1 << nbits); i++) {
                BitWriter_PutBits(&strm, i, nbits);
            }
            BitStream_Close(&strm);

            /* 正しく書き込めているか？ */
            BitReader_Open(&strm, memory_image, sizeof(memory_image));
            is_ok = 1;
            for (i = 0; i < (1 << nbits); i++) {
                uint32_t buf;
                BitReader_GetBits(&strm, &buf, nbits);
                if (buf != i) {
                    is_ok = 0;
                    break;
                }
            }
            EXPECT_EQ(1, is_ok);
            BitStream_Close(&strm);
        }

    }

    /* Flushテスト */
    {
        struct BitStream strm;
        uint8_t memory_image[256] = { 0, };
        uint32_t bits;

        BitWriter_Open(&strm, memory_image, sizeof(memory_image));
        BitWriter_PutBits(&strm, 1, 1);
        BitWriter_PutBits(&strm, 1, 1);
        /* 2bitしか書いていないがフラッシュ */
        BitStream_Flush(&strm);
        EXPECT_EQ(0, strm.bit_buffer);
        EXPECT_EQ(32, strm.bit_count);
        BitStream_Close(&strm);

        /* 1バイトで先頭2bitだけが立っているはず */
        BitReader_Open(&strm, memory_image, sizeof(memory_image));
        BitReader_GetBits(&strm, &bits, 8);
        EXPECT_EQ(0xC0, bits);
        EXPECT_EQ(24, strm.bit_count);
        EXPECT_EQ(0xC0000000, strm.bit_buffer);
        EXPECT_EQ(&memory_image[4], strm.memory_p);
        BitStream_Flush(&strm);
        EXPECT_EQ(0, strm.bit_count);
        EXPECT_EQ(0, strm.bit_buffer);
        EXPECT_EQ(&memory_image[1], strm.memory_p);
        BitStream_Close(&strm);
    }

}

/* seek, tellなどのストリーム操作系APIテスト */
TEST(BitStreamTest, StreamOperationTest)
{
    /* Seek/Tellテスト */
    {
        struct BitStream strm;
        int32_t tell_result;
        uint8_t test_memory[8];

        /* テスト用に適当にデータ作成 */
        BitWriter_Open(&strm, test_memory, sizeof(test_memory));
        BitWriter_PutBits(&strm, 0xDEADBEAF, 32);
        BitWriter_PutBits(&strm, 0xABADCAFE, 32);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(8, tell_result);
        BitStream_Close(&strm);

        /* ビットリーダを使ったseek & tellテスト */
        BitReader_Open(&strm, test_memory, sizeof(test_memory));
        BitStream_Seek(&strm, 0, BITSTREAM_SEEK_SET);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(0, tell_result);
        BitStream_Seek(&strm, 1, BITSTREAM_SEEK_CUR);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(1, tell_result);
        BitStream_Seek(&strm, 2, BITSTREAM_SEEK_CUR);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(3, tell_result);
        BitStream_Seek(&strm, 0, BITSTREAM_SEEK_END);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(7, tell_result);
        BitStream_Close(&strm);

        /* ビットライタを使ったseek & tellテスト */
        BitWriter_Open(&strm, test_memory, sizeof(test_memory));
        BitStream_Seek(&strm, 0, BITSTREAM_SEEK_SET);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(0, tell_result);
        BitStream_Seek(&strm, 1, BITSTREAM_SEEK_CUR);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(1, tell_result);
        BitStream_Seek(&strm, 2, BITSTREAM_SEEK_CUR);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(3, tell_result);
        BitStream_Seek(&strm, 0, BITSTREAM_SEEK_END);
        BitStream_Tell(&strm, &tell_result);
        EXPECT_EQ(7, tell_result);
        BitStream_Close(&strm);
    }
}

/* ランレングス取得テスト */
TEST(BitStreamTest, GetZeroRunLengthTest)
{
    {
        struct BitStream strm;
        uint8_t data[256] = { 0, };
        uint32_t test_length, run;

        for (test_length = 0; test_length <= 65; test_length++) {
            /* ラン長だけ0を書き込み、1で止める */
            BitWriter_Open(&strm, data, sizeof(data));
            for (run = 0; run < test_length; run++) {
                BitWriter_PutBits(&strm, 0, 1);
            }
            BitWriter_PutBits(&strm, 1, 1);
            BitStream_Close(&strm);

            BitReader_Open(&strm, data, sizeof(data));
            BitReader_GetZeroRunLength(&strm, &run);
            EXPECT_EQ(test_length, run);
        }

        /* ラン長出力APIを使用 */
        for (test_length = 0; test_length <= 65; test_length++) {
            BitWriter_Open(&strm, data, sizeof(data));
            BitWriter_PutZeroRun(&strm, test_length);
            BitStream_Close(&strm);

            BitReader_Open(&strm, data, sizeof(data));
            BitReader_GetZeroRunLength(&strm, &run);
            EXPECT_EQ(test_length, run);
        }

        /* 連続したラン */
        BitWriter_Open(&strm, data, sizeof(data));
        for (test_length = 0; test_length <= 32; test_length++) {
            BitWriter_PutZeroRun(&strm, test_length);
        }
        BitStream_Close(&strm);
        BitReader_Open(&strm, data, sizeof(data));
        for (test_length = 0; test_length <= 32; test_length++) {
            BitReader_GetZeroRunLength(&strm, &run);
            EXPECT_EQ(test_length, run);
        }
        BitStream_Close(&strm);
    }
}
