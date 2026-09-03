#include "mme-sm.h"
#include "mme-s13-handler.h"
#include "nas-path.h"
#include "s1ap-path.h"

static mme_s13_result_e result_from_diameter(
                const uint32_t *dia_err, const uint32_t *dia_exp_err);
static mme_eir_action_e action_for_equipment(
                uint32_t equipment_status_code, mme_eir_t eir_config);

mme_s13_result_e mme_s13_validate_message(ogs_diam_s13_message_t *s13_message)
{
    ogs_assert(s13_message);

    if (s13_message->result_code != ER_DIAMETER_SUCCESS) {
        ogs_warn("ME-Identity-Check failed [%d]",
                    s13_message->result_code);
        return result_from_diameter(
                s13_message->err, s13_message->exp_err);
    }

    return MME_S13_RESULT_ALLOWED;
}

mme_s13_result_e mme_s13_validate_eca(
        ogs_diam_s13_eca_message_t eca_message, mme_eir_t eir_config)
{
    uint32_t code = eca_message.equipment_status_code;
    mme_eir_action_e action = action_for_equipment(code, eir_config);

    if (action != MME_EIR_ALLOW) {
        ogs_info("ME-Identity-Check rejected for "
                "equipment status code '%d'", code);
        return MME_S13_RESULT_DENIED;
    }

    /* Allowed, but log when the equipment is not whitelisted: greylist
     * and blacklist are only let through because the operator's policy
     * (greylist_action / blacklist_action) says so, never silently. */
    if (code != OGS_DIAM_S13_EQUIPMENT_WHITELIST)
        ogs_warn("ME-Identity-Check: equipment status code '%d' "
                "is not whitelisted but allowed by policy", code);
    else
        ogs_info("ME-Identity-Check accepted for "
                "equipment status code '%d'", code);

    return MME_S13_RESULT_ALLOWED;
}

mme_s13_result_e mme_s13_handle_eca(
        mme_ue_t *mme_ue, ogs_diam_s13_message_t *s13_message)
{
    mme_s13_result_e rc;

    ogs_assert(mme_ue);
    ogs_assert(s13_message);

    rc = mme_s13_validate_message(s13_message);
    if (rc != MME_S13_RESULT_ALLOWED)
        return rc;

    return mme_s13_validate_eca(s13_message->eca_message, mme_self()->eir);
}

/*
 * 3GPP TS 29.272 clause 7.4:
 * DIAMETER_ERROR_EQUIPMENT_UNKNOWN is the only application error the EIR
 * can return over S13. Every other failure means the EIR did not give a
 * verdict at all, so the decision is left to the operator policy rather
 * than being turned into a rejection here.
 */
static mme_s13_result_e result_from_diameter(
                const uint32_t *dia_err, const uint32_t *dia_exp_err)
{
    if (dia_exp_err && *dia_exp_err == OGS_DIAM_S13_ERROR_EQUIPMENT_UNKNOWN)
        return MME_S13_RESULT_DENIED;

    ogs_warn("No usable ME-Identity-Check verdict "
             "[Result-Code:%d Experimental-Result-Code:%d]",
             dia_err ? (int)*dia_err : -1,
             dia_exp_err ? (int)*dia_exp_err : -1);
    return MME_S13_RESULT_UNAVAILABLE;
}

static mme_eir_action_e action_for_equipment(
        uint32_t equipment_status_code, mme_eir_t eir_config)
{
    switch (equipment_status_code) {
    case OGS_DIAM_S13_EQUIPMENT_WHITELIST:
        return eir_config.whitelist_action;
    case OGS_DIAM_S13_EQUIPMENT_GREYLIST:
        return eir_config.greylist_action;
    case OGS_DIAM_S13_EQUIPMENT_BLACKLIST:
        return eir_config.blacklist_action;
    default:
        return MME_EIR_REJECT;   /* unrecognized status code: fail closed */
    }
}

void mme_s13_reject_ue(enb_ue_t *enb_ue, mme_ue_t *mme_ue)
{
    int r;

    if (mme_ue->nas_eps.type == MME_EPS_TYPE_ATTACH_REQUEST) {
        ogs_info("[%s] Attach reject [OGS_NAS_EMM_CAUSE:%d]",
                mme_ue->imsi_bcd, OGS_NAS_EMM_CAUSE_ILLEGAL_ME);
        r = nas_eps_send_attach_reject(
                enb_ue, mme_ue, OGS_NAS_EMM_CAUSE_ILLEGAL_ME,
                OGS_NAS_ESM_CAUSE_PROTOCOL_ERROR_UNSPECIFIED);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);
    } else if (mme_ue->nas_eps.type == MME_EPS_TYPE_TAU_REQUEST) {
        ogs_info("[%s] TAU reject [OGS_NAS_EMM_CAUSE:%d]",
                mme_ue->imsi_bcd, OGS_NAS_EMM_CAUSE_ILLEGAL_ME);
        r = nas_eps_send_tau_reject(
                enb_ue, mme_ue, OGS_NAS_EMM_CAUSE_ILLEGAL_ME);
        ogs_expect(r == OGS_OK);
        ogs_assert(r != OGS_ERROR);
    } else
        ogs_error("Invalid Type[%d]", mme_ue->nas_eps.type);

    r = s1ap_send_ue_context_release_command(enb_ue,
            S1AP_Cause_PR_nas, S1AP_CauseNas_normal_release,
            S1AP_UE_CTX_REL_UE_CONTEXT_REMOVE, 0);
    ogs_expect(r == OGS_OK);
    ogs_assert(r != OGS_ERROR);
}
