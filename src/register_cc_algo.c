/*
* Author: Christian Huitema
* Copyright (c) 2025, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "picoquic_register_cc_algo.h"

/* Add a test algorithm to the list of registered algorithms.
 */
#define TEST_ALG_MAX_NB 16

picoquic_congestion_algorithm_t const* picoquic_ns_test_algo_list[TEST_ALG_MAX_NB];

int picoquic_register_cc_algorithm(picoquic_congestion_algorithm_t const* test_alg)
{
    int ret = 0;

    if (picoquic_nb_congestion_control_algorithms > TEST_ALG_MAX_NB) {
        ret = -1;
    }
    else {
        picoquic_congestion_algorithm_t const* new_alg_list[TEST_ALG_MAX_NB];
        size_t alg_copied = 1;

        new_alg_list[0] = test_alg;
        for (size_t i = 0; i < picoquic_nb_congestion_control_algorithms; i++) {
            if (strcmp(picoquic_congestion_control_algorithms[i]->congestion_algorithm_id, test_alg->congestion_algorithm_id) != 0) {
                if (alg_copied >= TEST_ALG_MAX_NB) {
                    ret = -1;
                    break;
                }
                new_alg_list[alg_copied] = picoquic_congestion_control_algorithms[i];
                alg_copied++;
            }
        }
        if (ret == 0) {
            /* The list was prepared properly. Now, commit it. */
            for (size_t i = 0; i < alg_copied; i++) {
                picoquic_ns_test_algo_list[i] = new_alg_list[i];
            }
            for (size_t i = alg_copied; i < alg_copied; i++) {
                picoquic_ns_test_algo_list[i] = NULL;
            }

            picoquic_register_congestion_control_algorithms(picoquic_ns_test_algo_list, alg_copied);
        }
    }
    return ret;
}
