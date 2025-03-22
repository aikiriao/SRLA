#ifndef SRLA_H_INCLUDED
#define SRLA_H_INCLUDED

#include "srla_stdint.h"

/* フォーマットバージョン */
#define SRLA_FORMAT_VERSION         7

/* コーデックバージョン */
#define SRLA_CODEC_VERSION          12

/* ヘッダサイズ */
#define SRLA_HEADER_SIZE            29

/* 処理可能な最大チャンネル数 */
#define SRLA_MAX_NUM_CHANNELS       8

/* 最大係数次数 */
#define SRLA_MAX_COEFFICIENT_ORDER  255

/* パラメータプリセット数 */
#define SRLA_NUM_PARAMETER_PRESETS  7


/* API結果型 */
typedef enum SRLAApiResultTag {
    SRLA_APIRESULT_OK = 0,                  /* 成功                         */
    SRLA_APIRESULT_INVALID_ARGUMENT,        /* 無効な引数                   */
    SRLA_APIRESULT_INVALID_FORMAT,          /* 不正なフォーマット           */
    SRLA_APIRESULT_INSUFFICIENT_BUFFER,     /* バッファサイズが足りない     */
    SRLA_APIRESULT_INSUFFICIENT_DATA,       /* データが足りない             */
    SRLA_APIRESULT_PARAMETER_NOT_SET,       /* パラメータがセットされてない */
    SRLA_APIRESULT_DETECT_DATA_CORRUPTION,  /* データ破損を検知した         */
    SRLA_APIRESULT_NG                       /* 分類不能な失敗               */
} SRLAApiResult;

/* ヘッダ情報 */
struct SRLAHeader {
    uint32_t format_version;                        /* フォーマットバージョン         */
    uint32_t codec_version;                         /* エンコーダバージョン           */
    uint16_t num_channels;                          /* チャンネル数                   */
    uint32_t num_samples;                           /* 1チャンネルあたり総サンプル数  */
    uint32_t sampling_rate;                         /* サンプリングレート             */
    uint16_t bits_per_sample;                       /* サンプルあたりビット数         */
    uint32_t max_num_samples_per_block;             /* ブロックあたり最大サンプル数   */
    uint8_t preset;                                 /* パラメータプリセット         */
};

#endif /* SRLA_H_INCLUDED */
