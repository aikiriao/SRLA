#ifndef BITSTREAM_H_INCLUDED
#define BITSTREAM_H_INCLUDED

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>

#if CHAR_BIT != 8
#error "This program 8bit/byte system only."
#endif

/* マクロを使うか否か？ */
#define BITSTREAM_USE_MACROS 1

/* BitStream_Seek関数の探索コード */
#define BITSTREAM_SEEK_SET  (int32_t)SEEK_SET
#define BITSTREAM_SEEK_CUR  (int32_t)SEEK_CUR
#define BITSTREAM_SEEK_END  (int32_t)SEEK_END

/* 読みモードか？（0で書きモード） */
#define BITSTREAM_FLAGS_MODE_READ  (1 << 0)

/* valの下位nbitsを取得 */
#define BITSTREAM_GETLOWERBITS(val, nbits) ((val) & g_bitstream_lower_bits_mask[(nbits)])

/* ビットストリーム構造体 */
struct BitStream {
    uint32_t bit_buffer; /* ビットの一時バッファ */
    uint32_t bit_count; /* [Reader]バッファ残りビット数, [Writer]メモリ書き出しまでのビット数 */
    const uint8_t *memory_image; /* メモリ領域先頭 */
    const uint8_t *memory_tail;/* メモリ領域末尾 */
    size_t memory_size; /* メモリ領域サイズ */
    uint8_t *memory_p; /* メモリ読み書き位置 */
    uint8_t flags; /* 内部状態フラグ */
};

/* NLZ（最上位ビットから1に当たるまでのビット数）の計算 */
#if defined(__GNUC__)
/* ビルトイン関数を使用 */
#define BITSTREAM_NLZ(x) (((x) > 0) ? (uint32_t)__builtin_clz(x) : 32U)
#elif defined(_MSC_VER)
/* ビルトイン関数を使用 */
__inline uint32_t BITSTREAM_NLZ(uint32_t x)
{
    unsigned long result;
    if (!x) {
        return 32U;
    }
    _BitScanReverse(&result, x);
    return result ^ 31U;
}
#else
/* ソフトウェア実装を使用 */
#define BITSTREAM_NLZ(x) BitStream_NLZSoft(x)
#endif

/* NLZのソフトウェア実装 */
#ifdef __cplusplus
extern "C" uint32_t BitStream_NLZSoft(uint32_t x);
#else
uint32_t BitStream_NLZSoft(uint32_t x);
#endif

#if !defined(BITSTREAM_USE_MACROS)

#ifdef __cplusplus
extern "C" {
#endif

/* ビットリーダのオープン */
void BitReader_Open(struct BitStream *stream, const uint8_t *memory, size_t size);

/* ビットライタのオープン */
void BitWriter_Open(struct BitStream *stream, const uint8_t *memory, size_t size);

/* ビットストリームのクローズ */
void BitStream_Close(struct BitStream *stream);

/* シーク(fseek準拠) */
void BitStream_Seek(struct BitStream *stream, int32_t offset, int32_t origin);

/* 現在位置(ftell)準拠 */
void BitStream_Tell(struct BitStream *stream, int32_t *result);

/* valの右側（下位）nbits 出力（最大32bit出力可能） */
void BitWriter_PutBits(struct BitStream *stream, uint32_t val, uint32_t nbits);

/* 0のランに続いて終わりの1を出力 */
void BitWriter_PutZeroRun(struct BitStream *stream, uint32_t runlength);

/* nbits 取得（最大32bit）し、その値を右詰めして出力 */
void BitReader_GetBits(struct BitStream *stream, uint32_t *val, uint32_t nbits);

/* つぎの1にぶつかるまで読み込み、その間に読み込んだ0のランレングスを取得 */
void BitReader_GetZeroRunLength(struct BitStream *stream, uint32_t *runlength);

/* バッファにたまったビットをクリア */
void BitStream_Flush(struct BitStream *stream);

#ifdef __cplusplus
}
#endif

#else /* BITSTREAM_USE_MACROS */

#ifdef __cplusplus
extern "C" {
#endif

/* 下位ビットを取り出すためのマスク */
extern const uint32_t g_bitstream_lower_bits_mask[33];

/* ラン長のパターンテーブル */
extern const uint32_t g_bitstream_zerobit_runlength_table[0x100];

#ifdef __cplusplus
}
#endif

/* ビットリーダのオープン */
#define BitReader_Open(stream, memory, size)\
    do {\
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        assert((void *)(memory) != NULL);\
        \
        /* 内部状態リセット */\
        (stream)->flags = 0;\
        \
        /* バッファ初期化 */\
        (stream)->bit_count   = 0;\
        (stream)->bit_buffer  = 0;\
        \
        /* メモリセット */\
        (stream)->memory_image = (memory);\
        (stream)->memory_size  = (size);\
        (stream)->memory_tail  = (memory) + (size);\
        \
        /* 読み出し位置は先頭に */\
        (stream)->memory_p = (memory);\
        \
        /* 読みモードとしてセット */\
        (stream)->flags |= (uint8_t)BITSTREAM_FLAGS_MODE_READ;\
    } while (0)

/* ビットライタのオープン */
#define BitWriter_Open(stream, memory, size)\
    do {\
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        assert((void *)(memory) != NULL);\
        \
        /* 内部状態リセット */\
        (stream)->flags = 0;\
        \
        /* バッファ初期化 */\
        (stream)->bit_count   = 32;\
        (stream)->bit_buffer  = 0;\
        \
        /* メモリセット */\
        (stream)->memory_image = (memory);\
        (stream)->memory_size  = (size);\
        (stream)->memory_tail  = (memory) + (size);\
        \
        /* 読み出し位置は先頭に */\
        (stream)->memory_p = (memory);\
        \
        /* 書きモードとしてセット */\
        (stream)->flags &= (uint8_t)(~BITSTREAM_FLAGS_MODE_READ);\
    } while (0)

/* ビットストリームのクローズ */
#define BitStream_Close(stream)\
    do {\
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        \
        /* 残ったデータをフラッシュ */\
        BitStream_Flush(stream);\
        \
        /* バッファのクリア */\
        (stream)->bit_buffer = 0;\
        \
        /* メモリ情報のクリア */\
        (stream)->memory_image = NULL;\
        (stream)->memory_size  = 0;\
        \
        /* 内部状態のクリア */\
        (stream)->memory_p     = NULL;\
        (stream)->flags        = 0;\
    } while (0)

/* シーク(fseek準拠) */
#define BitStream_Seek(stream, offset, origin)\
    do {\
        uint8_t* __pos = NULL;\
        \
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        \
        /* 内部バッファをクリア（副作用が起こる） */\
        BitStream_Flush(stream);\
        \
        /* 起点をまず定める */\
        switch (origin) {\
        case BITSTREAM_SEEK_CUR:\
            __pos = (stream)->memory_p;\
            break;\
        case BITSTREAM_SEEK_SET:\
            __pos = (uint8_t *)(stream)->memory_image;\
            break;\
        case BITSTREAM_SEEK_END:\
            __pos = (uint8_t *)((stream)->memory_tail - 1);\
            break;\
        default:\
            assert(0);\
        }\
        \
        /* オフセット分動かす */\
        __pos += (offset);\
        \
        /* 範囲チェック */\
        assert(__pos >= (stream)->memory_image);\
        assert(__pos < (stream)->memory_tail);\
        \
        /* 結果の保存 */\
        (stream)->memory_p = __pos;\
    } while (0)

/* 現在位置(ftell)準拠 */
#define BitStream_Tell(stream, result)\
    do {\
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        assert((void *)(result) != NULL);\
        \
        /* アクセスオフセットを返す */\
        (*result) = (int32_t)\
        ((stream)->memory_p - (stream)->memory_image);\
    } while (0)

/* valの右側（下位）nbits 出力（最大32bit出力可能） */
#define BitWriter_PutBits(stream, val, nbits)\
    do {\
        uint32_t __nbits;\
        \
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        \
        /* 読み込みモードでは実行不可能 */\
        assert(!((stream)->flags & BITSTREAM_FLAGS_MODE_READ));\
        \
        /* 出力可能な最大ビット数を越えてないか確認 */\
        assert((nbits) <= 32);\
        \
        /* 0ビット出力は何もせず終了 */\
        if ((nbits) == 0) { break; }\
        \
        /* valの上位ビットから順次出力 */\
        __nbits = (nbits);\
        if (__nbits >= (stream)->bit_count) {\
            __nbits -= (stream)->bit_count;\
            (stream)->bit_buffer |= BITSTREAM_GETLOWERBITS((val) >> __nbits, (stream)->bit_count);\
            \
            /* 終端に達していないかチェック */\
            assert((stream)->memory_p >= (stream)->memory_image);\
            assert(((stream)->memory_p + 3) < (stream)->memory_tail);\
            \
            /* メモリに書き出し */\
            (stream)->memory_p[0] = (uint8_t)(((stream)->bit_buffer >> 24) & 0xFF);\
            (stream)->memory_p[1] = (uint8_t)(((stream)->bit_buffer >> 16) & 0xFF);\
            (stream)->memory_p[2] = (uint8_t)(((stream)->bit_buffer >>  8) & 0xFF);\
            (stream)->memory_p[3] = (uint8_t)(((stream)->bit_buffer >>  0) & 0xFF);\
            (stream)->memory_p += 4;\
            \
            /* バッファをリセット */\
            (stream)->bit_buffer = 0;\
            (stream)->bit_count = 32;\
        }\
        \
        /* 端数ビットの処理: 残った分をバッファの上位ビットにセット */\
        assert(__nbits <= 32);\
        (stream)->bit_count -= __nbits;\
        (stream)->bit_buffer |= BITSTREAM_GETLOWERBITS(val, __nbits) << (stream)->bit_count;\
    } while (0)

/* 0のランに続いて終わりの1を出力 */
#define BitWriter_PutZeroRun(stream, runlength)\
    do {\
        uint32_t __run = ((runlength) + 1);\
        \
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        \
        /* 読み込みモードでは実行不可能 */\
        assert(!((stream)->flags & BITSTREAM_FLAGS_MODE_READ));\
        \
        /* 31ビット単位で出力 */\
        while (__run > 31) {\
            BitWriter_PutBits(stream, 0, 31);\
            __run -= 31;\
        }\
        /* 終端の1を出力 */\
        BitWriter_PutBits(stream, 1, __run);\
    } while (0)

/* nbits 取得（最大32bit）し、その値を右詰めして出力 */
#define BitReader_GetBits(stream, val, nbits)\
    do {\
        uint32_t __tmp, __nbits;\
        \
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        assert((void *)(val) != NULL);\
        \
        /* 読み込みモードでない場合はアサート */\
        assert((stream)->flags & BITSTREAM_FLAGS_MODE_READ);\
        \
        /* 入力可能な最大ビット数を越えてないか確認 */\
        assert((nbits) <= 32);\
        \
        /* 0ビット取得は0を返す */\
        if ((nbits) == 0) {\
            (*(val)) = 0;\
            break;\
        }\
        \
        /* バッファから取り出す */\
        if ((nbits) <= (stream)->bit_count) {\
            (stream)->bit_count -= (nbits);\
            (*(val)) = BITSTREAM_GETLOWERBITS((stream)->bit_buffer >> (stream)->bit_count, (nbits));\
            break;\
        }\
        \
        /* 現在のバッファ容量よりも多くのビットが要求されたらメモリから読み出し */\
        __nbits = (nbits);\
        /* 残りのビットを上位ビットにセット */\
        __nbits -= (stream)->bit_count;\
        __tmp = BITSTREAM_GETLOWERBITS((stream)->bit_buffer, (stream)->bit_count) << __nbits;\
        \
        /* 終端に達していないかチェック */\
        assert((stream)->memory_p >= (stream)->memory_image);\
        assert((stream)->memory_p < (stream)->memory_tail);\
        \
        /* メモリから読み出し */\
        (stream)->bit_buffer\
            = ((uint32_t)(stream)->memory_p[0] << 24) | ((uint32_t)(stream)->memory_p[1] << 16)\
            | ((uint32_t)(stream)->memory_p[2] <<  8) | ((uint32_t)(stream)->memory_p[3] <<  0);\
        (stream)->memory_p += 4;\
        (stream)->bit_count = 32;\
        \
        /* 端数ビットの処理 残ったビット分をtmpの最上位ビットにセット */\
        (stream)->bit_count -= __nbits;\
        __tmp |= BITSTREAM_GETLOWERBITS((stream)->bit_buffer >> (stream)->bit_count, __nbits);\
        \
        /* 正常終了 */\
        (*(val)) = __tmp;\
    } while (0)

/* つぎの1にぶつかるまで読み込み、その間に読み込んだ0のランレングスを取得 */
#define BitReader_GetZeroRunLength(stream, runlength)\
    do {\
        uint32_t __run;\
        \
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        assert((void *)(runlength) != NULL);\
        \
        /* 上位ビットからの連続する0を計測 */\
        __run = BITSTREAM_NLZ(BITSTREAM_GETLOWERBITS((stream)->bit_buffer, (stream)->bit_count)) + (stream)->bit_count - 32;\
        \
        /* 読み込んだ分カウントを減らす */\
        assert((stream)->bit_count >= __run);\
        (stream)->bit_count -= __run;\
        \
        /* バッファが空の時 */\
        while ((stream)->bit_count == 0) {\
            /* 1バイト読み込み再度計測 */\
            uint32_t __tmp_run;\
            \
            /* 終端に達していないかチェック */\
            assert((stream)->memory_p >= (stream)->memory_image);\
            assert((stream)->memory_p < (stream)->memory_tail);\
            \
            /* メモリから読み出し ビットバッファにセットし直して再度ランを計測 */\
            (stream)->bit_buffer = (stream)->memory_p[0];\
            (stream)->memory_p++;\
            /* テーブルによりラン長を取得 */\
            __tmp_run = g_bitstream_zerobit_runlength_table[(stream)->bit_buffer];\
            (stream)->bit_count = 8 - __tmp_run;\
            /* ランを加算 */\
            __run += __tmp_run;\
        }\
        \
        /* 続く1を空読み */\
        assert((stream)->bit_count >= 1);\
        (stream)->bit_count -= 1;\
        \
        /* 正常終了 */\
        (*(runlength)) = __run;\
    } while (0)

/* バッファにたまったビットをクリア */
#define BitStream_Flush(stream)\
    do {\
        /* 引数チェック */\
        assert((void *)(stream) != NULL);\
        \
        if ((stream)->flags & BITSTREAM_FLAGS_MODE_READ) {\
            /* バッファに余ったバイト分だけ読み出し位置を戻し、バッファクリア */\
            (stream)->memory_p -= ((stream)->bit_count >> 3);\
            (stream)->bit_buffer = 0;\
            (stream)->bit_count = 0;\
        } else {\
            if ((stream)->bit_count < 32) {\
                /* 次のバイト境界まで出力 */\
                const uint32_t __remainbits = 32 - (stream)->bit_count;\
                if (__remainbits > 24) {\
                    (stream)->memory_p[0] = (uint8_t)(((stream)->bit_buffer >> 24) & 0xFF);\
                    (stream)->memory_p[1] = (uint8_t)(((stream)->bit_buffer >> 16) & 0xFF);\
                    (stream)->memory_p[2] = (uint8_t)(((stream)->bit_buffer >>  8) & 0xFF);\
                    (stream)->memory_p[3] = (uint8_t)(((stream)->bit_buffer >>  0) & 0xFF);\
                    (stream)->memory_p += 4;\
                } else if (__remainbits > 16) {\
                    (stream)->memory_p[0] = (uint8_t)(((stream)->bit_buffer >> 24) & 0xFF);\
                    (stream)->memory_p[1] = (uint8_t)(((stream)->bit_buffer >> 16) & 0xFF);\
                    (stream)->memory_p[2] = (uint8_t)(((stream)->bit_buffer >>  8) & 0xFF);\
                    (stream)->memory_p += 3;\
                } else if (__remainbits > 8) {\
                    (stream)->memory_p[0] = (uint8_t)(((stream)->bit_buffer >> 24) & 0xFF);\
                    (stream)->memory_p[1] = (uint8_t)(((stream)->bit_buffer >> 16) & 0xFF);\
                    (stream)->memory_p += 2;\
                } else {\
                    (stream)->memory_p[0] = (uint8_t)(((stream)->bit_buffer >> 24) & 0xFF);\
                    (stream)->memory_p += 1;\
                }\
                (stream)->bit_count = 32;\
                (stream)->bit_buffer = 0;\
            }\
        }\
    } while (0)

#endif /* BITSTREAM_USE_MACROS */

#endif /* BITSTREAM_H_INCLUDED */
