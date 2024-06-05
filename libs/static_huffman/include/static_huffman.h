#ifndef STATICHUFFMAN_H_INCLUDED
#define STATICHUFFMAN_H_INCLUDED

#include <stdint.h>
#include "bit_stream.h"

/* 符号化するシンボルの最大数 */
#define STATICHUFFMAN_MAX_NUM_SYMBOLS 256

/* ハフマン木 */
struct StaticHuffmanTree {
    uint32_t num_symbols;                       /* 符号化シンボル数         */
    uint32_t root_node;                         /* 根ノードのインデックス   */
    struct {
        uint32_t node_0;                        /* 左側の子供のインデックス */
        uint32_t node_1;                        /* 右側の子供のインデックス */
    } nodes[2 * STATICHUFFMAN_MAX_NUM_SYMBOLS]; /* 木のノード               */
};

/* ハフマン符号 */
struct StaticHuffmanCodes {
    uint32_t num_symbols;                    /* 符号化シンボル数              */
    struct {
        uint32_t code;                       /* 割り当てたコード（最長32bit） */
        uint8_t bit_count;                   /* コード長                      */
    } codes[STATICHUFFMAN_MAX_NUM_SYMBOLS];  /* 各シンボルの符号              */
};

#ifdef __cplusplus
extern "C" {
#endif

/* ハフマン木の構築 */
void StaticHuffman_BuildHuffmanTree(
        const uint32_t *symbol_counts, uint32_t num_symbols, struct StaticHuffmanTree *tree);

/* 符号テーブル作成 */
void StaticHuffman_ConvertTreeToCodes(
        const struct StaticHuffmanTree *tree, struct StaticHuffmanCodes *codes);

/* ハフマン符号の出力 */
void StaticHuffman_PutCode(
        const struct StaticHuffmanCodes *codes, struct BitStream *stream, uint32_t val);

/* ハフマン符号の取得 */
uint32_t StaticHuffman_GetCode(
        const struct StaticHuffmanTree *tree, struct BitStream *stream);

#ifdef __cplusplus
}
#endif

#endif /* STATICHUFFMAN_H_INCLUDED */
