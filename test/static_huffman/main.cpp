#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/static_huffman/src/static_huffman.c"
}

#if 0
/* ノードの再帰的表示 */
static void StaticHuffman_PrintNode(
    const struct StaticHuffmanTree *tree, const uint32_t *counts,
    uint32_t node, uint32_t code, uint8_t bit_count)
{
    if (node < tree->num_symbols) {
        printf("%X counts:%d code:%X len:%d \n", node, counts[node], code, bit_count);
        return;
    }

    StaticHuffman_PrintNode(tree, counts, tree->nodes[node].node_0, (code << 1) | 0, bit_count + 1);
    StaticHuffman_PrintNode(tree, counts, tree->nodes[node].node_1, (code << 1) | 1, bit_count + 1);
}

/* 全ノード表示 */
static void StaticHuffman_PrintAllNodes(
    const struct StaticHuffmanTree *tree, const uint32_t *counts)
{
    StaticHuffman_PrintNode(tree, counts, tree->root_node, 0, 0);
}
#endif

/* ハフマン木・符号構成テスト */
TEST(StaticHuffmanTest, BuildHuffmanTreeTest)
{

    /* 簡単な成功例1 */
    {
        static const uint32_t counts[] = { 4, 3, 2, 1 };
        uint32_t counts_size = sizeof(counts) / sizeof(counts[0]);
        struct StaticHuffmanTree tree;
        struct StaticHuffmanCodes codes;

        StaticHuffman_BuildHuffmanTree(counts, counts_size, &tree);
        StaticHuffman_ConvertTreeToCodes(&tree, &codes);

        EXPECT_EQ(counts_size, tree.num_symbols);
        EXPECT_EQ(0x0, codes.codes[0].code); EXPECT_EQ(1, codes.codes[0].bit_count);
        EXPECT_EQ(0x2, codes.codes[1].code); EXPECT_EQ(2, codes.codes[1].bit_count);
        EXPECT_EQ(0x7, codes.codes[2].code); EXPECT_EQ(3, codes.codes[2].bit_count);
        EXPECT_EQ(0x6, codes.codes[3].code); EXPECT_EQ(3, codes.codes[3].bit_count);
    }

    /* 簡単な成功例2 */
    {
        static const uint32_t counts[] = { 5, 3, 2, 1, 1 };
        uint32_t counts_size = sizeof(counts) / sizeof(counts[0]);
        struct StaticHuffmanTree tree;
        struct StaticHuffmanCodes codes;

        StaticHuffman_BuildHuffmanTree(counts, counts_size, &tree);
        StaticHuffman_ConvertTreeToCodes(&tree, &codes);

        EXPECT_EQ(counts_size, tree.num_symbols);
        EXPECT_EQ(0x0, codes.codes[0].code); EXPECT_EQ(1, codes.codes[0].bit_count);
        EXPECT_EQ(0x2, codes.codes[1].code); EXPECT_EQ(2, codes.codes[1].bit_count);
        EXPECT_EQ(0x6, codes.codes[2].code); EXPECT_EQ(3, codes.codes[2].bit_count);
        EXPECT_EQ(0xE, codes.codes[3].code); EXPECT_EQ(4, codes.codes[3].bit_count);
        EXPECT_EQ(0xF, codes.codes[4].code); EXPECT_EQ(4, codes.codes[4].bit_count);
    }

    /* 平均符号長による確認 */
    {
#define NUM_ARRAY_ELEMENTS(array) sizeof(array) / sizeof(array[0])
        struct HuffmanCodeTestCase {
            const uint32_t *symbol_counts;
            uint32_t num_symbols;
            uint32_t sum_code_length_answer;
        };
        uint32_t test;
        static const uint32_t counts1[] = { 8, 4, 4, 4, 2, 2 };
        static const uint32_t counts2[] = { 50, 20, 10, 8, 5, 4, 2, 1 };
        static const struct HuffmanCodeTestCase test_cases[] = {
            { counts1, NUM_ARRAY_ELEMENTS(counts1),  60, },
            { counts2, NUM_ARRAY_ELEMENTS(counts2), 220, },
        };
        const uint32_t num_test_cases = NUM_ARRAY_ELEMENTS(test_cases);

        for (test = 0; test < num_test_cases; test++) {
            uint32_t symbol, sum_code_length;
            struct StaticHuffmanTree tree;
            struct StaticHuffmanCodes codes;
            const struct HuffmanCodeTestCase *ptest = &test_cases[test];

            StaticHuffman_BuildHuffmanTree(ptest->symbol_counts, ptest->num_symbols, &tree);
            StaticHuffman_ConvertTreeToCodes(&tree, &codes);
            ASSERT_EQ(ptest->num_symbols, tree.num_symbols);

            sum_code_length = 0;
            for (symbol = 0; symbol < tree.num_symbols; symbol++) {
                sum_code_length += ptest->symbol_counts[symbol] * codes.codes[symbol].bit_count;
            }
            EXPECT_EQ(ptest->sum_code_length_answer, sum_code_length);
        }
#undef NUM_ARRAY_ELEMENTS
    }
}

/* エンコードデコードテスト */
TEST(StaticHuffmanTest, PutGetCodeTest)
{
    /* 簡単な例 */
    {
#define NUM_SYMBOLS 100
        uint32_t symbol;
        uint32_t counts[NUM_SYMBOLS];
        uint8_t buffer[NUM_SYMBOLS];
        struct StaticHuffmanTree tree;
        struct StaticHuffmanCodes codes;
        struct BitStream stream;

        /* 全シンボルが等しく出てくるとする */
        for (symbol = 0; symbol < NUM_SYMBOLS; symbol++) {
            counts[symbol] = 1;
        }

        StaticHuffman_BuildHuffmanTree(counts, NUM_SYMBOLS, &tree);
        StaticHuffman_ConvertTreeToCodes(&tree, &codes);

        /* 出力 */
        BitWriter_Open(&stream, buffer, NUM_SYMBOLS);
        for (symbol = 0; symbol < NUM_SYMBOLS; symbol++) {
            StaticHuffman_PutCode(&codes, &stream, symbol);
        }
        BitStream_Flush(&stream);

        /* 符号取得/一致確認 */
        BitReader_Open(&stream, buffer, NUM_SYMBOLS);
        for (symbol = 0; symbol < NUM_SYMBOLS; symbol++) {
            const uint32_t test = StaticHuffman_GetCode(&tree, &stream);
            EXPECT_EQ(symbol, test);
        }
#undef NUM_SYMBOLS
    }

    /* テキストを圧縮・展開してみる */
    {
#define FILENAME "test.txt"
        FILE *fp;
        uint32_t counts[256];
        uint8_t *data, *buffer;
        uint32_t i, file_size, output_size;
        struct stat filestat;
        struct StaticHuffmanTree tree;
        struct StaticHuffmanCodes codes;
        struct BitStream stream;

        /* ファイルサイズ計算 */
        stat(FILENAME, &filestat);
        file_size = filestat.st_size;
        data = (uint8_t *)malloc(file_size);
        buffer = (uint8_t *)malloc(2 * file_size); /* 2倍までは増えないと想定 */

        /* ファイルデータ読み出し */
        fp = fopen(FILENAME, "rb");
        assert(fp != NULL);
        fread(data, sizeof(uint8_t), file_size, fp);
        fclose(fp);

        /* シンボル頻度計測 */
        memset(counts, 0, sizeof(uint32_t) * 256);
        for (i = 0; i < file_size; i++) {
            counts[data[i] & 0xFF]++;
        }

        StaticHuffman_BuildHuffmanTree(counts, 256, &tree);
        StaticHuffman_ConvertTreeToCodes(&tree, &codes);

        /* 出力 */
        BitWriter_Open(&stream, buffer, file_size);
        for (i = 0; i < file_size; i++) {
            StaticHuffman_PutCode(&codes, &stream, data[i]);
        }
        BitStream_Flush(&stream);

        BitStream_Tell(&stream, (int32_t *)&output_size);

        /* 符号取得/一致確認 */
        BitReader_Open(&stream, buffer, output_size);
        for (i = 0; i < file_size; i++) {
            const uint32_t test = StaticHuffman_GetCode(&tree, &stream);
            EXPECT_EQ(data[i], test);
        }

        free(data);
        free(buffer);
#undef FILENAME
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
