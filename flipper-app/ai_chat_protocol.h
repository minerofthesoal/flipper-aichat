#pragma once
// Deliberately NOT JSON on this side of the wire: the Flipper has no JSON
// lib in the base SDK and very little RAM, so the Flipper<->Devboard link
// uses a flat, line-based, pipe-delimited protocol. The ESP32 bridge is the
// one that speaks JSON to LM Studio. See docs/PROTOCOL.md for the full spec.

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AiChatMsgToken, // TOK|<chunk>            streamed assistant text
    AiChatMsgDone, // DONE                    end of a response
    AiChatMsgError, // ERR|<message>
    AiChatMsgModels, // MODELS|<a>,<b>,<c>     comma separated model ids
    AiChatMsgInfo, // INFO|<message>          status/log line
    AiChatMsgUnknown,
} AiChatMsgType;

typedef struct {
    AiChatMsgType type;
    FuriString* payload; // caller owns, must free with furi_string_free
} AiChatMsg;

// Parses a raw line received from the devboard. Always returns a valid
// AiChatMsg; unrecognized lines come back as AiChatMsgUnknown with the raw
// line copied into payload (useful for a debug/log pane).
AiChatMsg ai_chat_protocol_parse(const char* line);

void ai_chat_msg_free(AiChatMsg* msg);

// Builds the outbound "CHAT|<text>" line. Caller frees the returned FuriString.
FuriString* ai_chat_protocol_build_chat(const char* text);

// Builds "SELMODEL|<name>"
FuriString* ai_chat_protocol_build_select_model(const char* name);

#ifdef __cplusplus
}
#endif
