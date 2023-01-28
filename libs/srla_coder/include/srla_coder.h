#ifndef SRLACODER_H_INCLUDED
#define SRLACODER_H_INCLUDED

#include <stdint.h>
#include "bit_stream.h"

/* 符号化ハンドル */
struct SRLACoder;

#ifdef __cplusplus
extern "C" {
#endif

/* 符号化ハンドルの作成に必要なワークサイズの計算 */
int32_t SRLACoder_CalculateWorkSize(void);

/* 符号化ハンドルの作成 */
struct SRLACoder* SRLACoder_Create(void *work, int32_t work_size);

/* 符号化ハンドルの破棄 */
void SRLACoder_Destroy(struct SRLACoder *coder);

/* 符号付き整数配列の符号化 */
void SRLACoder_Encode(struct SRLACoder *coder, struct BitStream *stream, const int32_t *data, uint32_t num_samples);

/* 符号付き整数配列の復号 */
void SRLACoder_Decode(struct BitStream *stream, int32_t *data, uint32_t num_samples);

#ifdef __cplusplus
}
#endif

#endif /* SRLACODER_H_INCLUDED */
