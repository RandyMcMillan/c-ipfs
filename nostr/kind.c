#include "ipfs/nostr/kind.h"

const char* nostr_kind_description(int kind) {
    switch (kind) {
        case NOSTR_KIND_SET_METADATA:      return "set_metadata";
        case NOSTR_KIND_TEXT_NOTE:         return "text_note";
        case NOSTR_KIND_RECOMMEND_RELAY:   return "recommend_relay";
        case NOSTR_KIND_CONTACTS:          return "contacts";
        case NOSTR_KIND_ENCRYPTED_DM:      return "encrypted_dm";
        case NOSTR_KIND_DELETION:          return "deletion";
        case NOSTR_KIND_REPOST:            return "repost";
        case NOSTR_KIND_REACTION:          return "reaction";
        case NOSTR_KIND_CHANNEL_CREATE:    return "channel_create";
        case NOSTR_KIND_CHANNEL_METADATA:  return "channel_metadata";
        case NOSTR_KIND_CHANNEL_MESSAGE:   return "channel_message";
        case NOSTR_KIND_CHANNEL_HIDE:      return "channel_hide";
        case NOSTR_KIND_CHANNEL_MUTE:      return "channel_mute";
        case NOSTR_KIND_IPFS_CONTENT:      return "ipfs_content";
        case NOSTR_KIND_IPFS_PROVIDER:     return "ipfs_provider";
        case NOSTR_KIND_IPFS_PIN_REQUEST:  return "ipfs_pin_request";
        case NOSTR_KIND_IPFS_PIN_CONFIRM:  return "ipfs_pin_confirm";
        case NOSTR_KIND_PIP_ACK:           return "pip_ack";
        case NOSTR_KIND_PIP_REQUEST:       return "pip_request";
        case NOSTR_KIND_PIP_MANIFEST:      return "pip_manifest";
        case NOSTR_KIND_PIP_SLICE:         return "pip_slice";
        case NOSTR_KIND_PIP_ATTEST:        return "pip_attest";
        case NOSTR_KIND_PIP_SEAL:          return "pip_seal";
        case NOSTR_KIND_PIP_JOIN:          return "pip_join";
        case NOSTR_KIND_GIT_REPO:          return "git_repo";
        case NOSTR_KIND_GIT_STATE:         return "git_state";
        case NOSTR_KIND_GIT_PATCH:         return "git_patch";
        case NOSTR_KIND_GIT_ISSUE:         return "git_issue";
        case NOSTR_KIND_GIT_STATUS_OPEN:   return "git_status_open";
        case NOSTR_KIND_GIT_STATUS_MERGED: return "git_status_merged";
        case NOSTR_KIND_GIT_STATUS_CLOSED: return "git_status_closed";
        case NOSTR_KIND_GIT_STATUS_DRAFT:  return "git_status_draft";
        case NOSTR_KIND_GIT_GRASP_LIST:    return "git_grasp_list";
        default:                           return "unknown";
    }
}
