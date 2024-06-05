#include "static_huffman.h"
#include <stdint.h>
#include <string.h>

/* カウントの正規化 */
static void StaticHuffman_NormalizeSymbolCounts(
    const uint32_t *symbol_counts, uint32_t num_symbols,
    uint32_t *normalized_counts, uint32_t normalized_counts_size)
{
    uint32_t node;

    assert((symbol_counts != NULL) && (normalized_counts != NULL));
    assert((2 * num_symbols) <= normalized_counts_size);

    /* 頻度を作業領域にコピー */
    memset(normalized_counts, 0, sizeof(uint32_t) * normalized_counts_size);
    memcpy(normalized_counts, symbol_counts, sizeof(uint32_t) * num_symbols);

    /* 0（無効値）の回避 */
    for (node = 0; node < num_symbols; node++) {
        if (normalized_counts[node] == 0) {
            normalized_counts[node] += 1;
        }
    }
}

/* ハフマン符号の構築 */
void StaticHuffman_BuildHuffmanTree(
    const uint32_t *symbol_counts, uint32_t num_symbols, struct StaticHuffmanTree *tree)
{
#define SENTINEL_NODE (2 * STATICHUFFMAN_MAX_NUM_SYMBOLS)
    uint32_t min1, min2;  /* min1が最小頻度、min2が2番目の最小頻度  */
    uint32_t free_node, node;
    uint32_t counts_work[(2 * STATICHUFFMAN_MAX_NUM_SYMBOLS) + 1]; /* シンボルの頻度（番兵ノードで1つ分追加） */

    assert((symbol_counts != NULL) && (tree != NULL));
    assert(num_symbols > 0);
    assert(num_symbols <= STATICHUFFMAN_MAX_NUM_SYMBOLS);

    /* シンボル数を設定 */
    tree->num_symbols = num_symbols;

    /* 頻度カウントの正規化 */
    StaticHuffman_NormalizeSymbolCounts(
        symbol_counts, num_symbols, counts_work, 2 * STATICHUFFMAN_MAX_NUM_SYMBOLS);
    /* 番兵ノードに最大頻度を設定 */
    counts_work[SENTINEL_NODE] = UINT32_MAX;

    /* 親ノードの作成: num_symbols以降のノードを使用 */
    for (free_node = num_symbols; ; free_node++) {
        /* インデックスを番兵に設定 */
        min1 = min2 = SENTINEL_NODE;

        /* 1,2番目の最小値を与えるインデックスを求める */
        /* 最初は全てのノードから始め、次回以降は2つのノードを含んだ
        * 親ノードも含める形で最小値を与えるインデックスを探す */
        for (node = 0; node < free_node; node++) {
            /* 注目しているノードの頻度が無効値(0)でない時のみ参照 */
            if (counts_work[node] > 0) {
                if (counts_work[node] < counts_work[min1]) {
                    min2 = min1;
                    min1 = node;
                } else if (counts_work[node] < counts_work[min2]) {
                    min2 = node;
                }
            }
        }
        assert(min1 != SENTINEL_NODE);

        /* 2番目の最小値が見つからなかった */
        /* -> ノードが1つになるまでまとまっている。木の根が求まった状態 */
        if (min2 == SENTINEL_NODE) {
            break;
        }

        /* 親ノードに情報を詰める */
        /* 親ノードの頻度は子の和 */
        counts_work[free_node] = counts_work[min1] + counts_work[min2];
        /* 子ノードの頻度は無効値に */
        counts_work[min1] = counts_work[min2] = 0;
        /* 子ノードのインデックスを記録 */
        tree->nodes[free_node].node_0 = min1;
        tree->nodes[free_node].node_1 = min2;
    }

    assert(free_node <= (2 * STATICHUFFMAN_MAX_NUM_SYMBOLS));

    /* for文でインクリメントした後なので、1減らして根を記録 */
    tree->root_node = free_node - 1;

#undef SENTINEL_NODE
}

/* ハフマン木から符号を構築 */
static void StaticHuffman_ConvertTreeToCodesCore(
    const struct StaticHuffmanTree *tree, struct StaticHuffmanCodes *codes,
    uint32_t node, uint32_t code, uint8_t bit_count)
{
    assert(tree != NULL);
    assert(codes != NULL);

    /* 参照しているノードインデックスが葉に到達している */
    if (node < tree->num_symbols) {
        /* コードとビット数を割り当てる */
        codes->codes[node].code = code;
        codes->codes[node].bit_count = bit_count;
        return;
    }

    /* 符号を1ビット分長くする */
    code <<= 1;
    bit_count++;

    /* 左側の葉を辿る 符号の最下位ビットには0が追記される */
    StaticHuffman_ConvertTreeToCodesCore(tree, codes, tree->nodes[node].node_0, code | 0, bit_count);
    /* 右側の葉を辿る 符号の最下位ビットには1が追記される */
    StaticHuffman_ConvertTreeToCodesCore(tree, codes, tree->nodes[node].node_1, code | 1, bit_count);
}

/* 符号テーブル作成 */
void StaticHuffman_ConvertTreeToCodes(
    const struct StaticHuffmanTree *tree, struct StaticHuffmanCodes *codes)
{
    assert((tree != NULL) && (codes != NULL));

    /* シンボル数の記録 */
    codes->num_symbols = tree->num_symbols;

    /* 再帰処理を根から開始 */
    StaticHuffman_ConvertTreeToCodesCore(tree, codes, tree->root_node, 0, 0);
}

/* ハフマン符号の出力 */
void StaticHuffman_PutCode(
    const struct StaticHuffmanCodes *codes, struct BitStream *stream, uint32_t val)
{
    assert(codes != NULL);
    assert(stream != NULL);
    assert(val < codes->num_symbols);

    BitWriter_PutBits(stream, codes->codes[val].code, codes->codes[val].bit_count);
}

/* ハフマン符号の取得 */
uint32_t StaticHuffman_GetCode(
    const struct StaticHuffmanTree *tree, struct BitStream *stream)
{
    uint32_t node, bit;

    assert(tree != NULL);
    assert(stream != NULL);

    /* ノードをルートに設定 */
    node = tree->root_node;

    /* 葉ノードに達するまで木を辿る */
    do {
        BitReader_GetBits(stream, &bit, 1);
        node = (bit == 0) ? tree->nodes[node].node_0 : tree->nodes[node].node_1;
    } while (node >= tree->num_symbols);

    assert(node < tree->num_symbols);

    return node;
}
