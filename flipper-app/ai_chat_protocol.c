#include "ai_chat_protocol.h"
#include <string.h>

static const char* find_delim(const char* s) {
    return strchr(s, '|');
}

AiChatMsg ai_chat_protocol_parse(const char* line) {
    AiChatMsg msg = {.type = AiChatMsgUnknown, .payload = furi_string_alloc()};
    if(!line || !*line) return msg;

    const char* delim = find_delim(line);
    size_t tag_len = delim ? (size_t)(delim - line) : strlen(line);
    const char* rest = delim ? delim + 1 : "";

    if(tag_len == 3 && strncmp(line, "TOK", 3) == 0) {
        msg.type = AiChatMsgToken;
        furi_string_set(msg.payload, rest);
    } else if(tag_len == 4 && strncmp(line, "DONE", 4) == 0) {
        msg.type = AiChatMsgDone;
    } else if(tag_len == 3 && strncmp(line, "ERR", 3) == 0) {
        msg.type = AiChatMsgError;
        furi_string_set(msg.payload, rest);
    } else if(tag_len == 6 && strncmp(line, "MODELS", 6) == 0) {
        msg.type = AiChatMsgModels;
        furi_string_set(msg.payload, rest);
    } else if(tag_len == 4 && strncmp(line, "INFO", 4) == 0) {
        msg.type = AiChatMsgInfo;
        furi_string_set(msg.payload, rest);
    } else {
        furi_string_set(msg.payload, line);
    }
    return msg;
}

void ai_chat_msg_free(AiChatMsg* msg) {
    if(msg && msg->payload) {
        furi_string_free(msg->payload);
        msg->payload = NULL;
    }
}

FuriString* ai_chat_protocol_build_chat(const char* text) {
    FuriString* out = furi_string_alloc();
    furi_string_printf(out, "CHAT|%s", text ? text : "");
    return out;
}

FuriString* ai_chat_protocol_build_select_model(const char* name) {
    FuriString* out = furi_string_alloc();
    furi_string_printf(out, "SELMODEL|%s", name ? name : "");
    return out;
}
