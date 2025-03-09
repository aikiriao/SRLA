#ifndef SRLA_INTERNAL_H_INCLUDED
#define SRLA_INTERNAL_H_INCLUDED

#include "srla.h"
#include "srla_stdint.h"
#include "static_huffman.h"

/* 本ライブラリのメモリアラインメント */
#define SRLA_MEMORY_ALIGNMENT 16
/* ブロック先頭の同期コード */
#define SRLA_BLOCK_SYNC_CODE 0xFFFF

/* 内部エンコードパラメータ */
/* プリエンファシスの係数シフト量 */
#define SRLA_PREEMPHASIS_COEF_SHIFT 4
/* プリエンファシスフィルタの適用回数 */
#define SRLA_NUM_PREEMPHASIS_FILTERS 2
/* LPC係数のビット幅 */
#define SRLA_LPC_COEFFICIENT_BITWIDTH 8
/* LPC係数右シフト量のビット幅 */
#define SRLA_RSHIFT_LPC_COEFFICIENT_BITWIDTH 4
/* (LPC係数次数-1)のビット幅 */
#define SRLA_LPC_COEFFICIENT_ORDER_BITWIDTH 8
/* LPCのリッジ正則化パラメータ */
#define SRLA_LPC_RIDGE_REGULARIZATION_PARAMETER 1e-5
/* LTP係数次数 */
#define SRLA_LTP_ORDER 3
/* LTPの最小ピッチ周期 */
#define SRLA_LTP_MIN_PERIOD 20
/* LTPの最大ピッチ周期 */
#define SRLA_LTP_MAX_PERIOD SRLA_MAX_COEFFICIENT_ORDER
/* LTP係数のビット幅 */
#define SRLA_LTP_COEFFICIENT_BITWIDTH 8

/* アサートマクロ */
#ifdef NDEBUG
/* 未使用変数警告を明示的に回避 */
#define SRLA_ASSERT(condition) ((void)(condition))
#else
#include <assert.h>
#define SRLA_ASSERT(condition) assert(condition)
#endif

/* 静的アサートマクロ */
#define SRLA_STATIC_ASSERT(expr) extern void assertion_failed(char dummy[(expr) ? 1 : -1])

/* ブロックデータタイプ */
typedef enum SRLABlockDataTypeTag {
    SRLA_BLOCK_DATA_TYPE_COMPRESSDATA  = 0, /* 圧縮済みデータ */
    SRLA_BLOCK_DATA_TYPE_SILENT        = 1, /* 無音データ     */
    SRLA_BLOCK_DATA_TYPE_RAWDATA       = 2, /* 生データ       */
    SRLA_BLOCK_DATA_TYPE_INVALID       = 3  /* 無効           */
} SRLABlockDataType;

/* マルチチャンネル処理の決定方法 */
typedef enum SRLAChannelProcessMethodTacticsTag {
    SRLA_CH_PROCESS_METHOD_TACTICS_NONE = 0, /* 何もしない */
    SRLA_CH_PROCESS_METHOD_TACTICS_MS_FIXED, /* ステレオMS処理を常に選択 */
    SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, /* 適応的にLR,LS,RS,MSを選択 */
    SRLA_CH_PROCESS_METHOD_TACTICS_INVALID   /* 無効値 */
} SRLAChannelProcessMethodTactics;

/* マルチチャンネル処理法 */
typedef enum SRLAChannelProcessMethodTag {
    SRLA_CH_PROCESS_METHOD_NONE = 0, /* 何もしない */
    SRLA_CH_PROCESS_METHOD_MS = 1, /* ステレオMS処理 */
    SRLA_CH_PROCESS_METHOD_LS = 2, /* ステレオLS処理 */
    SRLA_CH_PROCESS_METHOD_SR = 3, /* ステレオSR処理 */
    SRLA_CH_PROCESS_METHOD_INVALID /* 無効値 */
} SRLAChannelProcessMethod;

/* LPCの次数決定方法 */
typedef enum SRLAChannelLPCOrderDecisionTacticsTag {
    SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED = 0, /* 最大次数を常に選択 */
    SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_SEARCH, /* 素朴な網羅探索 */
    SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION, /* 残差分散の推定による網羅探索 */
    SRLA_LPC_ORDER_DECISION_TACTICS_INVALID  /* 無効値 */
} SRLAChannelLPCOrderDecisionTactics;

/* 内部エラー型 */
typedef enum SRLAErrorTag {
    SRLA_ERROR_OK = 0, /* OK */
    SRLA_ERROR_NG, /* 分類不能な失敗 */
    SRLA_ERROR_INVALID_ARGUMENT, /* 不正な引数 */
    SRLA_ERROR_INVALID_FORMAT, /* 不正なフォーマット       */
    SRLA_ERROR_INSUFFICIENT_BUFFER, /* バッファサイズが足りない */
    SRLA_ERROR_INSUFFICIENT_DATA /* データサイズが足りない   */
} SRLAError;

/* パラメータプリセット */
struct SRLAParameterPreset {
    uint32_t max_num_parameters; /* 最大パラメータ数 */
    SRLAChannelProcessMethodTactics ch_process_method_tactics; /* マルチチャンネル処理の決定法 */
    SRLAChannelLPCOrderDecisionTactics lpc_order_tactics; /* LPCの次数決定法 */
    uint32_t svr_max_num_iterations; /* SVRの最大繰り返し回数 */
    const double *margin_list; /* マージンリスト */
    uint32_t margin_list_size; /* マージンリストサイズ */
};

#ifdef __cplusplus
extern "C" {
#endif

/* パラメータプリセット配列 */
extern const struct SRLAParameterPreset g_srla_parameter_preset[];

/* パラメータ符号用のハフマン木を取得 */
const struct StaticHuffmanTree* SRLA_GetParameterHuffmanTree(void);

/* 和をとったパラメータ符号用のハフマン木を取得 */
const struct StaticHuffmanTree *SRLA_GetSumParameterHuffmanTree(void);


#ifdef __cplusplus
}
#endif

#endif /* SRLA_INTERNAL_H_INCLUDED */
