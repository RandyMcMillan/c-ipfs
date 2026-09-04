#pragma once

/**
 * Nostr event kinds.
 * Standard kinds per NIP-01 and extensions.
 * IPFS hybrid kinds occupy the 1064-1069 range.
 * NIP-34 git kinds occupy the 30617-30618, 1617-1621, 1630-1633, 10317 ranges.
 */

/* Standard kinds (NIP-01) */
#define NOSTR_KIND_SET_METADATA      0
#define NOSTR_KIND_TEXT_NOTE         1
#define NOSTR_KIND_RECOMMEND_RELAY   2
#define NOSTR_KIND_CONTACTS          3
#define NOSTR_KIND_ENCRYPTED_DM      4
#define NOSTR_KIND_DELETION          5
#define NOSTR_KIND_REPOST            6
#define NOSTR_KIND_REACTION          7
#define NOSTR_KIND_BADGE_AWARD       8
#define NOSTR_KIND_CHANNEL_CREATE     40
#define NOSTR_KIND_CHANNEL_METADATA   41
#define NOSTR_KIND_CHANNEL_MESSAGE    42
#define NOSTR_KIND_CHANNEL_HIDE       43
#define NOSTR_KIND_CHANNEL_MUTE       44

/* Ephemeral / seals */
#define NOSTR_KIND_SEAL              13
#define NOSTR_KIND_DM_SEAL           1059 /* giftwrap */

/* IPFS hybrid protocol kinds (1064-1069 reserved) */
#define NOSTR_KIND_IPFS_CONTENT      1064  /* Announce IPFS CID availability */
#define NOSTR_KIND_IPFS_PROVIDER     1065  /* Node provider record over Nostr */
#define NOSTR_KIND_IPFS_PIN_REQUEST  1066  /* Request pin for CID */
#define NOSTR_KIND_IPFS_PIN_CONFIRM  1067  /* Confirm pin for CID */

/* PIP / NIP-PIP transfer kinds (39076-39082) */
#define NOSTR_KIND_PIP_ACK           39076 /* Transfer ACK / NAK */
#define NOSTR_KIND_PIP_REQUEST       39077 /* Transfer request */
#define NOSTR_KIND_PIP_MANIFEST      39078 /* Transfer manifest */
#define NOSTR_KIND_PIP_SLICE         39079 /* Transfer slice */
#define NOSTR_KIND_PIP_ATTEST        39080 /* Blob attestation */
#define NOSTR_KIND_PIP_SEAL          39081 /* Quorum seal */
#define NOSTR_KIND_PIP_JOIN          39082 /* Quorum membership */

/* NIP-34 git stuff kinds */
#define NOSTR_KIND_GIT_REPO          30617 /* Repository announcement */
#define NOSTR_KIND_GIT_STATE         30618 /* Repository state (refs) */
#define NOSTR_KIND_GIT_PATCH         1617  /* Git format-patch */
#define NOSTR_KIND_GIT_ISSUE         1621  /* Issue / PR markdown */
#define NOSTR_KIND_GIT_STATUS_OPEN   1630  /* Status: open */
#define NOSTR_KIND_GIT_STATUS_MERGED 1631  /* Status: merged/resolved */
#define NOSTR_KIND_GIT_STATUS_CLOSED 1632  /* Status: closed */
#define NOSTR_KIND_GIT_STATUS_DRAFT  1633  /* Status: draft */
#define NOSTR_KIND_GIT_GRASP_LIST    10317 /* Grasp server list */

const char* ipfs_nostr_kind_description(int kind);
