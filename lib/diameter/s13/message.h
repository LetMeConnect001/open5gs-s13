


#if !defined(OGS_DIAMETER_INSIDE) && !defined(OGS_DIAMETER_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_DIAM_S13_MESSAGE_H
#define OGS_DIAM_S13_MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* RFC 5516 / TS 29.272 §6) */
#define OGS_DIAM_S13_APPLICATION_ID 16777252

#define OGS_DIAM_S13_EQUIPMENT_WHITELIST                (0)
#define OGS_DIAM_S13_EQUIPMENT_BLACKLIST                (1)
#define OGS_DIAM_S13_EQUIPMENT_GREYLIST                 (2)

extern struct dict_object *ogs_diam_s13_application;

extern struct dict_object *ogs_diam_s13_cmd_ecr;
extern struct dict_object *ogs_diam_s13_cmd_eca;

extern struct dict_object *ogs_diam_s13_terminal_information;
extern struct dict_object *ogs_diam_s13_imei;
extern struct dict_object *ogs_diam_s13_software_version;
extern struct dict_object *ogs_diam_s13_equipment_status;

typedef struct ogs_diam_s13_eca_message_s {
    uint32_t equipment_status_code;
} ogs_diam_s13_eca_message_t;

/* RFC 5516 / TS 29.272 §6) */
typedef struct ogs_diam_s13_message_s {
#define OGS_DIAM_S13_CMD_CODE_ME_IDENTITY_CHECK             324
#define OGS_DIAM_S13_ERROR_EQUIPMENT_UNKNOWN                5422
    uint16_t                   cmd_code;
    uint32_t                   result_code;
    uint32_t                   *err;
    uint32_t                   *exp_err;
    ogs_diam_s13_eca_message_t eca_message;
} ogs_diam_s13_message_t;

int ogs_diam_s13_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OGS_DIAM_S13_MESSAGE_H */