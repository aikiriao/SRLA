#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/srla_internal/src/srla_utility.c"
}

/* Fletcher16の計算テスト */
TEST(SRLAUtilityTest, CalculateFletcher16Test)
{
    /* リファレンス値と一致するか？ */
    {
        uint32_t i;
        uint16_t ret;

        /* テストケース */
        struct Fletcher16TestCaseForString {
            const char *string;
            size_t string_len;
            uint16_t answer;
        };

        static const struct Fletcher16TestCaseForString fletcher16_test_case[] = {
            {    "abcde", 5, 0xC8F0 },
            {   "abcdef", 6, 0x2057 },
            { "abcdefgh", 8, 0x0627 },
        };
        const uint32_t fletcher16_num_test_cases = sizeof(fletcher16_test_case) / sizeof(fletcher16_test_case[0]);

        for (i = 0; i < fletcher16_num_test_cases; i++) {
            ret = SRLAUtility_CalculateFletcher16CheckSum(
                (const uint8_t *)fletcher16_test_case[i].string, fletcher16_test_case[i].string_len);
            EXPECT_EQ(ret, fletcher16_test_case[i].answer);
        }
    }

    /* 実データでテスト */
    {
        struct stat fstat;
        uint32_t i, data_size;
        uint16_t ret;
        uint8_t* data;
        FILE* fp;

        /* テストケース */
        struct Fletcher16TestCaseForFile {
            const char* filename;
            uint16_t answer;
        };

        static const struct Fletcher16TestCaseForFile fletcher16_test_case[] = {
            { "a.wav",            0x4C08 },
            { "PriChanIcon.png",  0x6DA9 },
        };
        const uint32_t crc16ibm_num_test_cases
            = sizeof(fletcher16_test_case) / sizeof(fletcher16_test_case[0]);

        for (i = 0; i < crc16ibm_num_test_cases; i++) {
            stat(fletcher16_test_case[i].filename, &fstat);
            data_size = fstat.st_size;
            data = (uint8_t*)malloc(fstat.st_size * sizeof(uint8_t));

            fp = fopen(fletcher16_test_case[i].filename, "rb");
            fread(data, sizeof(uint8_t), data_size, fp);
            ret = SRLAUtility_CalculateFletcher16CheckSum(data, data_size);
            EXPECT_EQ(ret, fletcher16_test_case[i].answer);

            free(data);
            fclose(fp);
        }
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
