#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/wav/src/wav.c"
}

/* WAVファイルフォーマット取得テスト */
TEST(WAVTest, GetWAVFormatTest)
{

    /* 失敗テスト */
    {
        struct WAVFileFormat format;

        EXPECT_EQ(
                WAV_APIRESULT_NG,
                WAV_GetWAVFormatFromFile(NULL, &format));
        EXPECT_EQ(
                WAV_APIRESULT_NG,
                WAV_GetWAVFormatFromFile("a.wav", NULL));
        EXPECT_EQ(
                WAV_APIRESULT_NG,
                WAV_GetWAVFormatFromFile(NULL, NULL));

        /* 存在しないファイルを開こうとして失敗するか？ */
        EXPECT_EQ(
                WAV_APIRESULT_NG,
                WAV_GetWAVFormatFromFile("dummy.a.wav.wav", &format));
    }

    /* 実wavファイルからの取得テスト */
    {
        struct WAVFileFormat format;
        EXPECT_EQ(
                WAV_APIRESULT_OK,
                WAV_GetWAVFormatFromFile("a.wav", &format));
    }

}

/* WAVファイルデータ取得テスト */
TEST(WAVTest, CreateDestroyTest)
{

    /* 失敗テスト */
    {
        struct WAVFileFormat format;
        format.data_format      = (WAVDataFormat)255; /* 不正なフォーマット */
        format.bits_per_sample  = 16;
        format.num_channels     = 8;
        format.sampling_rate    = 48000;
        format.num_samples      = 48000 * 5;
        EXPECT_TRUE(WAV_Create(NULL) == NULL);
        EXPECT_TRUE(WAV_Create(&format) == NULL);
        EXPECT_TRUE(WAV_CreateFromFile(NULL) == NULL);
        EXPECT_TRUE(WAV_CreateFromFile("dummy.a.wav.wav") == NULL);
    }

    /* ハンドル作成 / 破棄テスト */
    {
        uint32_t  ch, is_ok;
        struct WAVFile*       wavfile;
        struct WAVFileFormat  format;
        format.data_format     = WAV_DATA_FORMAT_PCM;
        format.bits_per_sample = 16;
        format.num_channels    = 8;
        format.sampling_rate   = 48000;
        format.num_samples     = 48000 * 5;

        wavfile = WAV_Create(&format);
        EXPECT_TRUE(wavfile != NULL);
        EXPECT_TRUE(wavfile->data != NULL);
        EXPECT_EQ(
                0,
                memcmp(&wavfile->format, &format, sizeof(struct WAVFileFormat)));
        is_ok = 1;
        for (ch = 0; ch < wavfile->format.num_channels; ch++) {
            if (wavfile->data[ch] == NULL) {
                is_ok = 0;
                break;
            }
        }
        EXPECT_EQ(1, is_ok);

        WAV_Destroy(wavfile);
    }


    /* 実wavファイルからの取得テスト */
    {
        struct WAVFile* wavfile;

        wavfile = WAV_CreateFromFile("a.wav");
        EXPECT_TRUE(wavfile != NULL);
        WAV_Destroy(wavfile);
    }
}

/* WAVファイルデータ書き込みテスト */
TEST(WAVTest, WriteTest)
{
    /* 失敗テスト */
    {
        const char            test_filename[] = "test.wav";
        struct WAVWriter      writer;
        struct WAVFileFormat  format;
        FILE                  *fp;

        format.data_format     = (WAVDataFormat)0xFF;  /* 不正  */
        format.num_samples     = 0;     /* dummy */
        format.num_channels    = 0;     /* dummy */
        format.bits_per_sample = 0;     /* dummy */
        format.sampling_rate   = 0;     /* dummy */

        fp = fopen(test_filename, "wb");
        WAVWriter_Initialize(&writer, fp);

        EXPECT_NE(
                WAV_ERROR_OK,
                WAVWriter_PutWAVHeader(&writer, NULL));
        EXPECT_NE(
                WAV_ERROR_OK,
                WAVWriter_PutWAVHeader(NULL, &format));
        EXPECT_NE(
                WAV_ERROR_OK,
                WAVWriter_PutWAVHeader(NULL, NULL));
        EXPECT_NE(
                WAV_ERROR_OK,
                WAVWriter_PutWAVHeader(&writer, &format));

        WAVWriter_Finalize(&writer);
        fclose(fp);
    }

    /* PCMデータ書き出しテスト */
    {
        const char            test_filename[] = "test.wav";
        struct WAVWriter      writer;
        struct WAVFileFormat  format;
        FILE                  *fp;
        struct WAVFile*       wavfile;
        uint32_t              ch, sample;

        /* 適宜フォーマットを用意 */
        format.data_format     = WAV_DATA_FORMAT_PCM;
        format.num_samples     = 16;
        format.num_channels    = 1;
        format.sampling_rate   = 48000;
        format.bits_per_sample = 8;

        /* ハンドル作成 */
        wavfile = WAV_Create(&format);
        EXPECT_TRUE(wavfile != NULL);

        /* データを書いてみる */
        for (ch = 0; ch < format.num_channels; ch++) {
            for (sample = 0; sample < format.num_samples; sample++) {
                WAVFile_PCM(wavfile, sample, ch) = sample - (int32_t)format.num_samples / 2;
            }
        }

        fp = fopen(test_filename, "wb");
        WAVWriter_Initialize(&writer, fp);

        /* 不正なビット深度に書き換えて書き出し */
        /* -> 失敗を期待 */
        wavfile->format.bits_per_sample = 3;
        EXPECT_NE(
                WAV_ERROR_OK,
                WAVWriter_PutWAVPcmData(&writer, wavfile));

        /* 書き出し */
        wavfile->format.bits_per_sample = 8;
        EXPECT_EQ(
                WAV_ERROR_OK,
                WAVWriter_PutWAVPcmData(&writer, wavfile));

        WAVWriter_Finalize(&writer);
        fclose(fp);
    }

    /* 実ファイルを読み出してそのまま書き出してみる */
    {
        uint32_t ch, is_ok, i_test;
        const char* test_sourcefile_list[] = {
            "a.wav",
            "8bit.wav",
            "16bit.wav",
            "24bit.wav",
            "32bit.wav",
            "8bit_2ch.wav",
            "16bit_2ch.wav",
            "24bit_2ch.wav",
            "32bit_2ch.wav",
        };
        const char test_filename[] = "tmp.wav";
        struct WAVFile *src_wavfile, *test_wavfile;
        WAVApiResult ret;

        for (i_test = 0;
                i_test < sizeof(test_sourcefile_list) / sizeof(test_sourcefile_list[0]);
                i_test++) {
            /* 元になるファイルを読み込み */
            src_wavfile = WAV_CreateFromFile(test_sourcefile_list[i_test]);

            /* 読み込んだデータをそのままファイルへ書き出し */
            ret = WAV_WriteToFile(test_filename, src_wavfile);
            ASSERT_EQ(WAV_APIRESULT_OK, ret);

            /* 一度書き出したファイルを読み込んでみる */
            test_wavfile = WAV_CreateFromFile(test_filename);
            ASSERT_TRUE(test_wavfile != NULL);

            /* 最初に読み込んだファイルと一致するか？ */
            /* フォーマットの一致確認 */
            EXPECT_EQ(
                    0, memcmp(&src_wavfile->format, &test_wavfile->format, sizeof(struct WAVFileFormat)));

            /* PCMの一致確認 */
            is_ok = 1;
            for (ch = 0; ch < src_wavfile->format.num_channels; ch++) {
                if (memcmp(src_wavfile->data[ch], test_wavfile->data[ch],
                            sizeof(WAVPcmData) * src_wavfile->format.num_samples) != 0) {
                    is_ok = 0;
                    break;
                }
            }
            EXPECT_EQ(1, is_ok);

            /* ハンドル破棄 */
            WAV_Destroy(src_wavfile);
            WAV_Destroy(test_wavfile);
        }
    }

}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
