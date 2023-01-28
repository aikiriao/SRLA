#include "bit_stream.h"

/* マクロ定義を削除 */
#undef BitReader_Open
#undef BitWriter_Open
#undef BitStream_Close
#undef BitStream_Seek
#undef BitStream_Tell
#undef BitWriter_PutBits
#undef BitWriter_PutZeroRun
#undef BitReader_GetBits
#undef BitReader_GetZeroRunLength
#undef BitStream_Flush

/* マクロの代わりに関数宣言 */
extern "C" {
void BitReader_Open(struct BitStream *stream, const uint8_t *memory, size_t size);
void BitWriter_Open(struct BitStream *stream, const uint8_t *memory, size_t size);
void BitStream_Close(struct BitStream *stream);
void BitStream_Seek(struct BitStream *stream, int32_t offset, int32_t origin);
void BitStream_Tell(struct BitStream *stream, int32_t *result);
void BitWriter_PutBits(struct BitStream *stream, uint32_t val, uint32_t nbits);
void BitWriter_PutZeroRun(struct BitStream *stream, uint32_t runlength);
void BitReader_GetBits(struct BitStream *stream, uint32_t *val, uint32_t nbits);
void BitReader_GetZeroRunLength(struct BitStream *stream, uint32_t *runlength);
void BitStream_Flush(struct BitStream *stream);
}

/* 多重定義防止 */
#define BitStream_NLZSoft BitStream_NLZSoftTestDummy
#define g_bitstream_lower_bits_mask g_bitstream_lower_bits_mask_test_dummy
#define g_bitstream_zerobit_runlength_table g_bitstream_zerobit_runlength_table_test_dummy

/* テスト対象のモジュール */
extern "C" {
/* 関数定義を使用 */
#undef BITSTREAM_USE_MACROS
#include "../../libs/bit_stream/src/bit_stream.c"
}

/* マクロと同じテストソースを使用 */
#define BitStreamTest BitStreamFunctionTest
#include "bit_stream_common_test.cpp"
#undef BitStreamTest
