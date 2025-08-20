/*
register alternate CC algorithm
*/

#ifndef PICOQUIC_REGISTER_CC_ALGO
#define PICOQUIC_REGISTER_CC_ALGO

#include "picoquic.h"
#include "picoquic_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

    int picoquic_register_cc_algorithm(picoquic_congestion_algorithm_t const* test_alg);

#ifdef __cplusplus
}
#endif
#endif
