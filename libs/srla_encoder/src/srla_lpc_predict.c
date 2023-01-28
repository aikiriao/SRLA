#include "srla_lpc_predict.h"

#include <string.h>
#include "srla_internal.h"

/* LPC係数により予測/誤差出力 */
void SRLALPC_Predict(
    const int32_t *data, uint32_t num_samples,
    const int32_t *coef, uint32_t coef_order, int32_t *residual, uint32_t coef_rshift)
{
    uint32_t smpl, ord;
    int32_t predict;
    const int32_t half = 1 << (coef_rshift - 1); /* 固定小数の0.5 */

    /* 引数チェック */
    SRLA_ASSERT(data != NULL);
    SRLA_ASSERT(coef != NULL);
    SRLA_ASSERT(residual != NULL);

    memcpy(residual, data, sizeof(int32_t) * num_samples);

    /* 予測 */
    for (smpl = 0; smpl < num_samples - coef_order; smpl++) {
        predict = half;
        for (ord = 0; ord < coef_order; ord++) {
            predict += (coef[ord] * data[smpl + ord]);
        }
        residual[smpl + ord] += (predict >> coef_rshift);
    }
}
