#ifndef MME_S13_HANDLER_H
#define MME_S13_HANDLER_H

#include "mme-context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Outcome of the ME Identity Check.
 *
 * The EIR verdict and a failure to obtain one are deliberately kept
 * distinct: only the caller knows how the operator wants an unreachable
 * EIR to be treated (see mme_eir_t.failure_action).
 */
typedef enum {
    MME_S13_RESULT_ALLOWED = 0,   /* Equipment accepted: proceed to the ULR */
    MME_S13_RESULT_DENIED,        /* Equipment refused: reject the UE */
    MME_S13_RESULT_UNAVAILABLE,   /* No verdict: apply eir.failure_action */
} mme_s13_result_e;

mme_s13_result_e mme_s13_handle_eca(
        mme_ue_t *mme_ue, ogs_diam_s13_message_t *s13_message);

/* Shared reject path: same Attach/TAU Reject + release used by the SM
 * for a real EIR verdict, and by mme_s13_send_ecr() when missing_pei_action
 * rejects before any request is even sent. */

void mme_s13_reject_ue(enb_ue_t *enb_ue, mme_ue_t *mme_ue);

/* Exposed for testing purposes only; should not be called directly. */
mme_s13_result_e mme_s13_validate_message(
        ogs_diam_s13_message_t *s13_message);
mme_s13_result_e mme_s13_validate_eca(
        ogs_diam_s13_eca_message_t eca_message, const mme_eir_t *eir_config);

#ifdef __cplusplus
}
#endif

#endif /* MME_S13_HANDLER_H */
