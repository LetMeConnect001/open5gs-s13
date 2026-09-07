/*
 * Copyright (C) 2026 by LetMeConnect
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Isolated tests for the EIR (S13) decision logic and the IMEISV cache.
 *
 * Nothing here touches freeDiameter: mme_s13_validate_* are pure functions
 * of their inputs, mme_s13_handle_eca() only reads mme_self()->eir, and the
 * cache API works on the MME context alone. The Diameter wire path
 * (mme_s13_send_ecr / mme_s13_eca_cb / mme_s13_ecr_expire_cb) needs a
 * peer and is covered by scenario tests, not here.
 */

#include "mme/mme-s13-handler.h"
#include "core/abts.h"

/* Result-Code values not exported by the S13 library */
#define S13_TEST_UNABLE_TO_DELIVER       3002
#define S13_TEST_TOO_BUSY                3004
#define S13_TEST_UNKNOWN_EPS_SUBSCRIPTION 5420

static ogs_diam_s13_eca_message_t eca(uint32_t status)
{
    ogs_diam_s13_eca_message_t m;
    memset(&m, 0, sizeof(m));
    m.equipment_status_code = status;
    return m;
}

/*
 * Build a message the way mme_s13_eca_cb does: err/exp_err point into the
 * message's own result_code and exactly one of them is set.
 */
static void msg_result(ogs_diam_s13_message_t *m, uint32_t code)
{
    memset(m, 0, sizeof(*m));
    m->cmd_code = OGS_DIAM_S13_CMD_CODE_ME_IDENTITY_CHECK;
    m->result_code = code;
    m->err = &m->result_code;
}

static void msg_exp_result(ogs_diam_s13_message_t *m, uint32_t code)
{
    memset(m, 0, sizeof(*m));
    m->cmd_code = OGS_DIAM_S13_CMD_CODE_ME_IDENTITY_CHECK;
    m->result_code = code;
    m->exp_err = &m->result_code;
}

/* Equipment-Status x {greylist_action, blacklist_action} matrix */
static void s13_test_equipment_status_policy(abts_case *tc, void *data)
{
    mme_eir_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* Whitelisted: always allowed, whatever the knobs say */
    cfg.greylist_action = MME_EIR_REJECT;
    cfg.blacklist_action = MME_EIR_REJECT;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED,
            mme_s13_validate_eca(
                eca(OGS_DIAM_S13_EQUIPMENT_WHITELIST), &cfg));

    /* Greylisted follows greylist_action only */
    cfg.greylist_action = MME_EIR_ALLOW;
    cfg.blacklist_action = MME_EIR_REJECT;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED,
            mme_s13_validate_eca(
                eca(OGS_DIAM_S13_EQUIPMENT_GREYLIST), &cfg));
    cfg.greylist_action = MME_EIR_REJECT;
    cfg.blacklist_action = MME_EIR_ALLOW;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED,
            mme_s13_validate_eca(
                eca(OGS_DIAM_S13_EQUIPMENT_GREYLIST), &cfg));

    /* Blacklisted follows blacklist_action only */
    cfg.greylist_action = MME_EIR_REJECT;
    cfg.blacklist_action = MME_EIR_ALLOW;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED,
            mme_s13_validate_eca(
                eca(OGS_DIAM_S13_EQUIPMENT_BLACKLIST), &cfg));
    cfg.greylist_action = MME_EIR_ALLOW;
    cfg.blacklist_action = MME_EIR_REJECT;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED,
            mme_s13_validate_eca(
                eca(OGS_DIAM_S13_EQUIPMENT_BLACKLIST), &cfg));

    /* Unknown Equipment-Status value: fail closed regardless of policy */
    cfg.greylist_action = MME_EIR_ALLOW;
    cfg.blacklist_action = MME_EIR_ALLOW;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED,
            mme_s13_validate_eca(eca(3), &cfg));
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED,
            mme_s13_validate_eca(eca(0xffffffff), &cfg));
}

/* Result-Code / Experimental-Result -> ALLOWED | DENIED | UNAVAILABLE */
static void s13_test_diameter_result_mapping(abts_case *tc, void *data)
{
    ogs_diam_s13_message_t m;

    /* 2001: the verdict is in Equipment-Status, message itself is fine */
    msg_result(&m, ER_DIAMETER_SUCCESS);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED,
            mme_s13_validate_message(&m));

    /* Transport / protocol / permanent errors carry no verdict */
    msg_result(&m, S13_TEST_UNABLE_TO_DELIVER);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));
    msg_result(&m, S13_TEST_TOO_BUSY);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));
    msg_result(&m, ER_DIAMETER_UNABLE_TO_COMPLY);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));

    /* 5422 in Experimental-Result is the one application verdict */
    msg_exp_result(&m, OGS_DIAM_S13_ERROR_EQUIPMENT_UNKNOWN);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED,
            mme_s13_validate_message(&m));

    /* ...and only there: 5422 in Result-Code is not a verdict */
    msg_result(&m, OGS_DIAM_S13_ERROR_EQUIPMENT_UNKNOWN);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));

    /* Any other experimental error: no verdict */
    msg_exp_result(&m, S13_TEST_UNKNOWN_EPS_SUBSCRIPTION);
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));

    /* Non-success with neither pointer set (defensive): no verdict, no crash */
    memset(&m, 0, sizeof(m));
    m.result_code = ER_DIAMETER_UNABLE_TO_COMPLY;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_validate_message(&m));
}

/* Full handler: Result-Code gate first, then the list policy */
static void s13_test_handle_eca(abts_case *tc, void *data)
{
    ogs_diam_s13_message_t m;
    mme_ue_t *mme_ue;

    mme_ue = ogs_calloc(1, sizeof(*mme_ue)); /* only asserted non-NULL */
    ABTS_PTR_NOTNULL(tc, mme_ue);

    mme_self()->eir.greylist_action = MME_EIR_ALLOW;
    mme_self()->eir.blacklist_action = MME_EIR_REJECT;

    msg_result(&m, ER_DIAMETER_SUCCESS);
    m.eca_message.equipment_status_code = OGS_DIAM_S13_EQUIPMENT_WHITELIST;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED, mme_s13_handle_eca(mme_ue, &m));

    msg_result(&m, ER_DIAMETER_SUCCESS);
    m.eca_message.equipment_status_code = OGS_DIAM_S13_EQUIPMENT_GREYLIST;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_ALLOWED, mme_s13_handle_eca(mme_ue, &m));

    msg_result(&m, ER_DIAMETER_SUCCESS);
    m.eca_message.equipment_status_code = OGS_DIAM_S13_EQUIPMENT_BLACKLIST;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_DENIED, mme_s13_handle_eca(mme_ue, &m));

    /* An error Result-Code wins over whatever Equipment-Status is left */
    msg_result(&m, S13_TEST_UNABLE_TO_DELIVER);
    m.eca_message.equipment_status_code = OGS_DIAM_S13_EQUIPMENT_WHITELIST;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_handle_eca(mme_ue, &m));

    ogs_free(mme_ue);
}

/*
 * The exact message mme_s13_ecr_expire_cb synthesises when no ECA arrives
 * within eir.timeout must land in the failure_action branch, i.e. be
 * classified UNAVAILABLE by the full handler whatever the list policy.
 */
static void s13_test_timeout_is_unavailable(abts_case *tc, void *data)
{
    ogs_diam_s13_message_t *m;
    mme_ue_t *mme_ue;

    m = ogs_calloc(1, sizeof(*m));
    ABTS_PTR_NOTNULL(tc, m);
    m->cmd_code = OGS_DIAM_S13_CMD_CODE_ME_IDENTITY_CHECK;
    m->result_code = ER_DIAMETER_UNABLE_TO_COMPLY;
    m->err = &m->result_code;

    mme_ue = ogs_calloc(1, sizeof(*mme_ue));
    ABTS_PTR_NOTNULL(tc, mme_ue);

    mme_self()->eir.greylist_action = MME_EIR_REJECT;
    mme_self()->eir.blacklist_action = MME_EIR_REJECT;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_handle_eca(mme_ue, m));

    mme_self()->eir.greylist_action = MME_EIR_ALLOW;
    mme_self()->eir.blacklist_action = MME_EIR_ALLOW;
    ABTS_INT_EQUAL(tc, MME_S13_RESULT_UNAVAILABLE,
            mme_s13_handle_eca(mme_ue, m));

    ogs_free(mme_ue);
    ogs_free(m);
}

/*
 * mme_context_init() is needed by every test here, not only the cache
 * ones: it installs the "mme" log domain that mme-s13-handler.c logs to
 * (an ogs_warn() on an uninstalled domain is FATAL). It sizes its pools
 * from the static app and global configuration; the unit binary never
 * parses a config file, so give it small but non-zero sizes first.
 */
#define S13_TEST_MAX_UE 8

static void s13_context_setup(void)
{
    ogs_global_conf()->max.ue = S13_TEST_MAX_UE;
    ogs_global_conf()->max.peer = 2;
    ogs_app()->pool.nf = 2;
    ogs_app()->pool.csmap = 2;
    ogs_app()->pool.emerg = 2;
    ogs_app()->pool.sess = S13_TEST_MAX_UE;
    ogs_app()->pool.bearer = S13_TEST_MAX_UE;
    mme_context_init();

    /* mme_context_init() installs "mme" at the core default level; the
     * other unit suites run their domains at error (abts-main.c), and
     * every case below deliberately exercises the warn/info paths. */
    ogs_log_set_domain_level(__mme_log_domain, OGS_LOG_ERROR);
}

static void s13_test_cache_basic(abts_case *tc, void *data)
{
    mme_eir_cache_entry_t *e1, *e2, *again;
    ogs_time_t before;

    mme_eir_cache_remove_all();

    ABTS_TRUE(tc, mme_eir_cache_find("3512345678901201") == NULL);

    before = ogs_time_now();
    ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
            "001010123456789", "3512345678901201",
            OGS_DIAM_S13_EQUIPMENT_WHITELIST));
    e1 = mme_eir_cache_find("3512345678901201");
    ABTS_PTR_NOTNULL(tc, e1);
    ABTS_TRUE(tc, e1->valid);
    ABTS_INT_EQUAL(tc, OGS_DIAM_S13_EQUIPMENT_WHITELIST, e1->status);
    ABTS_STR_EQUAL(tc, "3512345678901201", e1->imeisv_bcd);
    ABTS_STR_EQUAL(tc, "001010123456789", e1->imsi_bcd);
    ABTS_TRUE(tc, e1->checked_at >= before);

    /* Same IMEISV again: updated in place, no second entry */
    ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
            "001010999999999", "3512345678901201",
            OGS_DIAM_S13_EQUIPMENT_BLACKLIST));
    again = mme_eir_cache_find("3512345678901201");
    ABTS_TRUE(tc, again == e1);
    ABTS_INT_EQUAL(tc, OGS_DIAM_S13_EQUIPMENT_BLACKLIST, again->status);
    ABTS_TRUE(tc, again->checked_at >= before);
    /* IMSI is informational, not the key: first writer's value is kept */
    ABTS_STR_EQUAL(tc, "001010123456789", again->imsi_bcd);

    /* A different IMEISV is a different entry */
    ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
            "001010123456789", "3512345678901202",
            OGS_DIAM_S13_EQUIPMENT_GREYLIST));
    e2 = mme_eir_cache_find("3512345678901202");
    ABTS_PTR_NOTNULL(tc, e2);
    ABTS_TRUE(tc, e2 != e1);
    ABTS_INT_EQUAL(tc, OGS_DIAM_S13_EQUIPMENT_GREYLIST, e2->status);
    ABTS_TRUE(tc, mme_eir_cache_find("3512345678901201") == e1);

    mme_eir_cache_remove_all();
    ABTS_TRUE(tc, mme_eir_cache_find("3512345678901201") == NULL);
    ABTS_TRUE(tc, mme_eir_cache_find("3512345678901202") == NULL);
}

/* The hash keys on the entry's own buffer, never on the caller's */
static void s13_test_cache_key_is_copied(abts_case *tc, void *data)
{
    char key[OGS_MAX_IMEISV_BCD_LEN+1];
    mme_eir_cache_entry_t *e;

    mme_eir_cache_remove_all();

    strcpy(key, "3599999999999901");
    ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
            "001010123456789", key, OGS_DIAM_S13_EQUIPMENT_WHITELIST));

    /* Clobber the caller's buffer: the entry must still be reachable */
    memset(key, 'x', OGS_MAX_IMEISV_BCD_LEN);
    e = mme_eir_cache_find("3599999999999901");
    ABTS_PTR_NOTNULL(tc, e);
    ABTS_STR_EQUAL(tc, "3599999999999901", e->imeisv_bcd);
    ABTS_TRUE(tc, mme_eir_cache_find(key) == NULL);

    mme_eir_cache_remove_all();
}

/*
 * Pool exhaustion. The pool holds max.ue * 2 entries (mme_context_init).
 * Existing keys stay updatable when full; a new key currently FAILS
 * because no eviction is implemented. When LRU eviction lands, flip the
 * last two expectations: OGS_OK, and the oldest key must be gone.
 */
static void s13_test_cache_pool_exhaustion(abts_case *tc, void *data)
{
    int i, n = S13_TEST_MAX_UE * 2;
    char imeisv[OGS_MAX_IMEISV_BCD_LEN+1];
    ogs_log_level_e level;

    mme_eir_cache_remove_all();

    for (i = 0; i < n; i++) {
        snprintf(imeisv, sizeof(imeisv), "35%014d", i);
        ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
                "001010123456789", imeisv, OGS_DIAM_S13_EQUIPMENT_WHITELIST));
    }
    for (i = 0; i < n; i++) {
        snprintf(imeisv, sizeof(imeisv), "35%014d", i);
        ABTS_PTR_NOTNULL(tc, mme_eir_cache_find(imeisv));
    }

    /* Existing key: still updatable */
    snprintf(imeisv, sizeof(imeisv), "35%014d", 0);
    ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
            "001010123456789", imeisv, OGS_DIAM_S13_EQUIPMENT_GREYLIST));
    ABTS_INT_EQUAL(tc, OGS_DIAM_S13_EQUIPMENT_GREYLIST,
            mme_eir_cache_find(imeisv)->status);

    /* New key on a full pool: no eviction yet. The "pool exhausted"
     * ogs_error() is the expected outcome here, keep it off the console. */
    snprintf(imeisv, sizeof(imeisv), "35%014d", n);
    level = ogs_log_get_domain_level(__mme_log_domain);
    ogs_log_set_domain_level(__mme_log_domain, OGS_LOG_FATAL);
    ABTS_INT_EQUAL(tc, OGS_ERROR, mme_eir_cache_update(
            "001010123456789", imeisv, OGS_DIAM_S13_EQUIPMENT_WHITELIST));
    ogs_log_set_domain_level(__mme_log_domain, level);
    ABTS_TRUE(tc, mme_eir_cache_find(imeisv) == NULL);

    /* remove_all gives every slot back */
    mme_eir_cache_remove_all();
    for (i = 0; i < n; i++) {
        snprintf(imeisv, sizeof(imeisv), "35%014d", i);
        ABTS_INT_EQUAL(tc, OGS_OK, mme_eir_cache_update(
                "001010123456789", imeisv, OGS_DIAM_S13_EQUIPMENT_WHITELIST));
    }
    mme_eir_cache_remove_all();
}

abts_suite *test_mme_s13(abts_suite *suite)
{
    suite = ADD_SUITE(suite)

    s13_context_setup();

    /* Decision logic */
    abts_run_test(suite, s13_test_equipment_status_policy, NULL);
    abts_run_test(suite, s13_test_diameter_result_mapping, NULL);
    abts_run_test(suite, s13_test_handle_eca, NULL);
    abts_run_test(suite, s13_test_timeout_is_unavailable, NULL);

    /* Cache */
    abts_run_test(suite, s13_test_cache_basic, NULL);
    abts_run_test(suite, s13_test_cache_key_is_copied, NULL);
    abts_run_test(suite, s13_test_cache_pool_exhaustion, NULL);
    mme_context_final();

    return suite;
}