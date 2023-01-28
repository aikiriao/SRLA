#include "bit_stream.h"
#include <stdint.h>

/* 下位ビットを取り出すマスク 32bitまで */
const uint32_t g_bitstream_lower_bits_mask[33] = {
    0x00000000U,
    0x00000001U, 0x00000003U, 0x00000007U, 0x0000000FU,
    0x0000001FU, 0x0000003FU, 0x0000007FU, 0x000000FFU,
    0x000001FFU, 0x000003FFU, 0x000007FFU, 0x00000FFFU,
    0x00001FFFU, 0x00003FFFU, 0x00007FFFU, 0x0000FFFFU,
    0x0001FFFFU, 0x0003FFFFU, 0x0007FFFFU, 0x000FFFFFU,
    0x001FFFFFU, 0x003FFFFFU, 0x007FFFFFU, 0x00FFFFFFU,
    0x01FFFFFFU, 0x03FFFFFFU, 0x07FFFFFFU, 0x0FFFFFFFU,
    0x1FFFFFFFU, 0x3FFFFFFFU, 0x7FFFFFFFU, 0xFFFFFFFFU
};

/* 0のラン長パターンテーブル（注意：上位ビットからのラン長） */
const uint32_t g_bitstream_zerobit_runlength_table[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* NLZ計算のためのテーブル */
#define UNUSED 99
static const uint32_t st_nlz10_table[64] = {
        32,     20,     19, UNUSED, UNUSED,     18, UNUSED,      7,
        10,     17, UNUSED, UNUSED,     14, UNUSED,      6, UNUSED,
    UNUSED,      9, UNUSED,     16, UNUSED, UNUSED,      1,     26,
    UNUSED,     13, UNUSED, UNUSED,     24,      5, UNUSED, UNUSED,
    UNUSED,     21, UNUSED,      8,     11, UNUSED,     15, UNUSED,
    UNUSED, UNUSED, UNUSED,      2,     27,      0,     25, UNUSED,
        22, UNUSED,     12, UNUSED, UNUSED,      3,     28, UNUSED,
        23, UNUSED,      4,     29, UNUSED, UNUSED,     30,     31
};
#undef UNUSED

#if !defined(BITSTREAM_USE_MACROS)

/* ビットリーダのオープン */
void BitReader_Open(struct BitStream *stream, const uint8_t *memory, size_t size)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(memory != NULL);

    /* 内部状態リセット */
    stream->flags = 0;

    /* バッファ初期化 */
    stream->bit_count   = 0;
    stream->bit_buffer  = 0;

    /* メモリセット */
    stream->memory_image = memory;
    stream->memory_size = size;
    stream->memory_tail = memory + size;

    /* 読み出し位置は先頭に */
    stream->memory_p = (uint8_t *)(memory);

    /* 読みモードとしてセット */
    stream->flags |= (uint8_t)BITSTREAM_FLAGS_MODE_READ;
}

/* ビットライタのオープン */
void BitWriter_Open(struct BitStream *stream, const uint8_t *memory, size_t size)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(memory != NULL);

    /* 内部状態リセット */
    stream->flags = 0;

    /* バッファ初期化 */
    stream->bit_count = 32;
    stream->bit_buffer = 0;

    /* メモリセット */
    stream->memory_image = memory;
    stream->memory_size = size;
    stream->memory_tail = memory + size;

    /* 読み出し位置は先頭に */
    stream->memory_p = (uint8_t *)(memory);

    /* 書きモードとしてセット */
    stream->flags &= (uint8_t)(~BITSTREAM_FLAGS_MODE_READ);
}

/* ビットストリームのクローズ */
void BitStream_Close(struct BitStream *stream)
{
    /* 引数チェック */
    assert(stream != NULL);

    /* 残ったデータをフラッシュ */
    BitStream_Flush(stream);

    /* バッファのクリア */
    stream->bit_buffer = 0;

    /* メモリ情報のクリア */
    stream->memory_image = NULL;
    stream->memory_size  = 0;

    /* 内部状態のクリア */
    stream->memory_p     = NULL;
    stream->flags        = 0;
}

/* シーク(fseek準拠) */
void BitStream_Seek(struct BitStream *stream, int32_t offset, int32_t origin)
{
    uint8_t *pos = NULL;

    /* 引数チェック */
    assert(stream != NULL);

    /* 内部バッファをクリア（副作用が起こる） */
    BitStream_Flush(stream);

    /* 起点をまず定める */
    switch (origin) {
    case BITSTREAM_SEEK_CUR:
        pos = stream->memory_p;
        break;
    case BITSTREAM_SEEK_SET:
        pos = (uint8_t *)stream->memory_image;
        break;
    case BITSTREAM_SEEK_END:
        pos = (uint8_t *)((stream)->memory_tail - 1);
        break;
    default:
        assert(0);
    }

    /* オフセット分動かす */
    pos += (offset);

    /* 範囲チェック */
    assert(pos >= stream->memory_image);
    assert(pos < (stream)->memory_tail);

    /* 結果の保存 */
    stream->memory_p = pos;
}

/* 現在位置(ftell)準拠 */
void BitStream_Tell(struct BitStream *stream, int32_t *result)
{
    /* 引数チェック */
    assert(stream != NULL);
    assert(result != NULL);

    /* アクセスオフセットを返す */
    (*result) = (int32_t)(stream->memory_p - stream->memory_image);
}

/* valの右側（下位）nbits 出力（最大32bit出力可能） */
void BitWriter_PutBits(struct BitStream *stream, uint32_t val, uint32_t nbits)
{
    /* 引数チェック */
    assert(stream != NULL);

    /* 読み込みモードでは実行不可能 */
    assert(!(stream->flags & BITSTREAM_FLAGS_MODE_READ));

    /* 出力可能な最大ビット数を越えてないか確認 */
    assert(nbits <= 32);

    /* 0ビット出力は何もせず終了 */
    if (nbits == 0) { return; }

    /* valの上位ビットから順次出力 */
    if (nbits >= stream->bit_count) {
        nbits -= stream->bit_count;
        stream->bit_buffer |= BITSTREAM_GETLOWERBITS(val >> nbits, stream->bit_count);

        /* 終端に達していないかチェック */
        assert(stream->memory_p >= stream->memory_image);
        assert((stream->memory_p + 3) < stream->memory_tail);

        /* メモリに書き出し */
        stream->memory_p[0] = ((stream->bit_buffer >> 24) & 0xFF);
        stream->memory_p[1] = ((stream->bit_buffer >> 16) & 0xFF);
        stream->memory_p[2] = ((stream->bit_buffer >>  8) & 0xFF);
        stream->memory_p[3] = ((stream->bit_buffer >>  0) & 0xFF);
        stream->memory_p += 4;

        /* バッファをリセット */
        stream->bit_buffer = 0;
        stream->bit_count = 32;
    }

    /* 端数ビットの処理: 残った分をバッファの上位ビットにセット */
    assert(nbits <= 32);
    stream->bit_count -= nbits;
    stream->bit_buffer |= BITSTREAM_GETLOWERBITS(val, nbits) << stream->bit_count;
}

/* 0のランに続いて終わりの1を出力 */
void BitWriter_PutZeroRun(struct BitStream *stream, uint32_t runlength)
{
    uint32_t run = runlength + 1;

    /* 引数チェック */
    assert(stream != NULL);

    /* 読み込みモードでは実行不可能 */
    assert(!(stream->flags & BITSTREAM_FLAGS_MODE_READ));

    /* 31ビット単位で出力 */
    while (run > 31) {
        BitWriter_PutBits(stream, 0, 31);
        run -= 31;
    }

    /* 終端の1を出力 */
    BitWriter_PutBits(stream, 1, run);
}

/* nbits 取得（最大32bit）し、その値を右詰めして出力 */
void BitReader_GetBits(struct BitStream *stream, uint32_t *val, uint32_t nbits)
{
    uint32_t tmp = 0;

    /* 引数チェック */
    assert(stream != NULL);
    assert(val != NULL);

    /* 読み込みモードでない場合はアサート */
    assert(stream->flags & BITSTREAM_FLAGS_MODE_READ);

    /* 入力可能な最大ビット数を越えてないか確認 */
    assert(nbits <= 32);

    /* 0ビット取得は0を返す */
    if (nbits == 0) {
        (*val) = 0;
        return;
    }

    /* バッファから取り出す */
    if (nbits <= stream->bit_count) {
        stream->bit_count -= nbits;
        (*val) = BITSTREAM_GETLOWERBITS(stream->bit_buffer >> stream->bit_count, nbits);
        return;
    }

    /* 現在のバッファ容量よりも多くのビットが要求されたらメモリから読み出し */

    /* 残りのビットを上位ビットにセット */
    nbits -= stream->bit_count;
    tmp = BITSTREAM_GETLOWERBITS(stream->bit_buffer, stream->bit_count) << nbits;

    /* 終端に達していないかチェック */
    assert(stream->memory_p >= stream->memory_image);
    assert(stream->memory_p < stream->memory_tail);

    /* メモリから読み出し */
    stream->bit_buffer
        = ((uint32_t)stream->memory_p[0] << 24) | ((uint32_t)stream->memory_p[1] << 16)
        | ((uint32_t)stream->memory_p[2] << 8) | ((uint32_t)stream->memory_p[3] << 0);
    stream->memory_p += 4;
    stream->bit_count = 32;

    /* 端数ビットの処理 残ったビット分をtmpの最上位ビットにセット */
    stream->bit_count -= nbits;
    tmp |= BITSTREAM_GETLOWERBITS(stream->bit_buffer >> stream->bit_count, nbits);

    /* 正常終了 */
    (*val) = tmp;
}

/* つぎの1にぶつかるまで読み込み、その間に読み込んだ0のランレングスを取得 */
void BitReader_GetZeroRunLength(struct BitStream *stream, uint32_t *runlength)
{
    uint32_t run;

    /* 引数チェック */
    assert(stream != NULL);
    assert(runlength != NULL);

    /* 上位ビットからの連続する0を計測 */
    run = BITSTREAM_NLZ(BITSTREAM_GETLOWERBITS(stream->bit_buffer, stream->bit_count)) + stream->bit_count - 32;

    /* 読み込んだ分カウントを減らす */
    assert(stream->bit_count >= run);
    stream->bit_count -= run;

    /* バッファが空の時 */
    while (stream->bit_count == 0) {
        /* 1バイト読み込み再度計測 */
        uint32_t tmp_run;

        /* 終端に達していないかチェック */
        assert(stream->memory_p >= stream->memory_image);
        assert(stream->memory_p < stream->memory_tail);

        /* メモリから読み出し ビットバッファにセットし直して再度ランを計測 */
        stream->bit_buffer = stream->memory_p[0];
        stream->memory_p++;
        /* テーブルによりラン長を取得 */
        tmp_run = g_bitstream_zerobit_runlength_table[stream->bit_buffer];
        stream->bit_count = 8 - tmp_run;
        /* ランを加算 */
        run += tmp_run;
    }

    /* 続く1を空読み */
    assert(stream->bit_count >= 1);
    stream->bit_count -= 1;

    /* 正常終了 */
    (*runlength) = run;
}

/* バッファにたまったビットをクリア（読み込み/書き込み位置を次のバイト境界に移動） */
void BitStream_Flush(struct BitStream *stream)
{
    /* 引数チェック */
    assert(stream != NULL);

    if (stream->flags & BITSTREAM_FLAGS_MODE_READ) {
        /* バッファに余ったバイト分だけ読み出し位置を戻し、バッファクリア */
        stream->memory_p -= (stream->bit_count >> 3);
        stream->bit_buffer = 0;
        stream->bit_count = 0;
    } else {
        if (stream->bit_count < 32) {
            /* 次のバイト境界まで出力 */
            const uint32_t remainbits = 32 - stream->bit_count;
            if (remainbits > 24) {
                stream->memory_p[0] = ((stream->bit_buffer >> 24) & 0xFF);
                stream->memory_p[1] = ((stream->bit_buffer >> 16) & 0xFF);
                stream->memory_p[2] = ((stream->bit_buffer >>  8) & 0xFF);
                stream->memory_p[3] = ((stream->bit_buffer >>  0) & 0xFF);
                stream->memory_p += 4;
            } else if (remainbits > 16) {
                stream->memory_p[0] = ((stream->bit_buffer >> 24) & 0xFF);
                stream->memory_p[1] = ((stream->bit_buffer >> 16) & 0xFF);
                stream->memory_p[2] = ((stream->bit_buffer >>  8) & 0xFF);
                stream->memory_p += 3;
            } else if (remainbits > 8) {
                stream->memory_p[0] = ((stream->bit_buffer >> 24) & 0xFF);
                stream->memory_p[1] = ((stream->bit_buffer >> 16) & 0xFF);
                stream->memory_p += 2;
            } else {
                stream->memory_p[0] = ((stream->bit_buffer >> 24) & 0xFF);
                stream->memory_p += 1;
            }
            stream->bit_count = 32;
            stream->bit_buffer = 0;
        }
    }
}

#endif /* BITSTREAM_USE_MACROS */

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
uint32_t BitStream_NLZSoft(uint32_t x)
{
    /* ハッカーのたのしみ参照 */
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x & ~(x >> 16);
    x = (x << 9) - x;
    x = (x << 11) - x;
    x = (x << 14) - x;
    return st_nlz10_table[x >> 26];
}
