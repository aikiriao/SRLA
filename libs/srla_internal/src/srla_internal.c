#include "srla_internal.h"

/* 配列の要素数を取得 */
#define SRLA_NUM_ARRAY_ELEMENTS(array) ((sizeof(array)) / (sizeof(array[0])))
/* プリセットの要素定義 */
#define SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(array) array, SRLA_NUM_ARRAY_ELEMENTS(array)

/* マージンリスト候補配列 */
static const double margin_list[] = { 0.0, 1.0 / 4096, 1.0 / 1024, 1.0 / 256, 1.0 / 64, 1.0 / 16 };

/* パラメータプリセット配列 */
const struct SRLAParameterPreset g_srla_parameter_preset[] = {
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_SEARCH,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  32, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_SEARCH, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  64, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  64, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  64, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    {  64, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    { 128, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    { 128, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_MAX_FIXED, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    { 128, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION,  0, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
    { 128, SRLA_CH_PROCESS_METHOD_TACTICS_ADAPTIVE, SRLA_LPC_ORDER_DECISION_TACTICS_BRUTEFORCE_ESTIMATION, 10, SRLA_DEFINE_ARRAY_AND_NUM_ELEMTNS_TUPLE(margin_list) },
};

SRLA_STATIC_ASSERT(SRLA_NUM_ARRAY_ELEMENTS(g_srla_parameter_preset) == SRLA_NUM_PARAMETER_PRESETS);
