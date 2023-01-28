#ifndef SRLA_LPCSYNTHESIZE_H_INCLUDED
#define SRLA_LPCSYNTHESIZE_H_INCLUDED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LPC係数により合成(in-place) */
void SRLALPC_Synthesize(
    int32_t *data, uint32_t num_samples,  const int32_t *coef, uint32_t coef_order, uint32_t coef_rshift);

#ifdef __cplusplus
}
#endif

#endif /* SRLA_LPCSYNTHESIZE_H_INCLUDED */
