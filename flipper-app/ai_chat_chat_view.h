#pragma once

#include <gui/view.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AiChatEventOpenKeyboard, // OK pressed on chat view
    AiChatEventOpenModelSelect, // Left pressed on chat view
    AiChatEventExit, // Back pressed on chat view
} AiChatChatViewEvent;

typedef void (*AiChatChatViewEventCallback)(AiChatChatViewEvent event, void* context);

View* ai_chat_chat_view_alloc(void);
void ai_chat_chat_view_free(View* view);

void ai_chat_chat_view_set_event_callback(
    View* view,
    AiChatChatViewEventCallback callback,
    void* context);

// Appends a full line to history, e.g. "You: hello" or "AI: hi there".
void ai_chat_chat_view_add_line(View* view, const char* text);

// Streaming API: call begin once, append repeatedly as tokens arrive, end
// when a DONE message is received. While streaming the view shows a
// spinner-ish "..." tail instead of the keyboard hint.
void ai_chat_chat_view_stream_begin(View* view, const char* speaker_prefix);
void ai_chat_chat_view_stream_append(View* view, const char* chunk);
void ai_chat_chat_view_stream_end(View* view);

void ai_chat_chat_view_set_status(View* view, const char* status);
void ai_chat_chat_view_set_model_name(View* view, const char* model_name);

#ifdef __cplusplus
}
#endif
