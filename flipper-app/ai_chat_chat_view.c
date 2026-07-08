#include "ai_chat_chat_view.h"
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HISTORY_LINES 64
#define WRAP_COLS 24 // chars per row at FontSecondary on a 128px wide canvas
#define VISIBLE_ROWS 6

typedef struct {
    FuriString* lines[MAX_HISTORY_LINES];
    size_t line_count;
    size_t scroll_offset; // rows scrolled up from the bottom

    bool streaming;
    FuriString* stream_buf; // "AI: partial tokens..."

    FuriString* status;
    FuriString* model_name;
} ChatViewModel;

typedef struct {
    AiChatChatViewEventCallback callback;
    void* callback_context;
} ChatViewContext;

static void chat_view_push_line(ChatViewModel* model, const char* text) {
    if(model->line_count == MAX_HISTORY_LINES) {
        furi_string_free(model->lines[0]);
        memmove(&model->lines[0], &model->lines[1], sizeof(FuriString*) * (MAX_HISTORY_LINES - 1));
        model->line_count--;
    }
    model->lines[model->line_count++] = furi_string_alloc_set(text);
}

// Very small greedy word-wrapper: splits `text` into <=WRAP_COLS chunks and
// pushes each as its own history line, so the draw routine stays trivial.
static void chat_view_push_wrapped(ChatViewModel* model, const char* text) {
    size_t len = strlen(text);
    size_t pos = 0;
    if(len == 0) {
        chat_view_push_line(model, "");
        return;
    }
    while(pos < len) {
        char buf[WRAP_COLS + 1];
        size_t chunk = (len - pos > WRAP_COLS) ? WRAP_COLS : (len - pos);
        // try to break on a space if we're mid-word and there's more to come
        if(pos + chunk < len) {
            size_t back = chunk;
            while(back > 0 && text[pos + back] != ' ') back--;
            if(back > 0) chunk = back;
        }
        memcpy(buf, text + pos, chunk);
        buf[chunk] = '\0';
        chat_view_push_line(model, buf);
        pos += chunk;
        while(pos < len && text[pos] == ' ') pos++;
    }
}

static void chat_view_draw_callback(Canvas* canvas, void* model_ptr) {
    ChatViewModel* model = model_ptr;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    // Header bar: model name + status
    canvas_draw_str(canvas, 0, 7, furi_string_get_cstr(model->model_name));
    canvas_draw_line(canvas, 0, 9, 127, 9);

    // History, bottom-anchored
    size_t total_rows = model->line_count + (model->streaming ? 1 : 0);
    size_t visible = VISIBLE_ROWS;
    size_t start = 0;
    if(total_rows > visible) {
        start = total_rows - visible - model->scroll_offset;
        if(start > total_rows) start = 0; // clamp on underflow
    }

    int y = 19;
    for(size_t row = start; row < total_rows && (row - start) < visible; row++) {
        const char* text;
        if(row < model->line_count) {
            text = furi_string_get_cstr(model->lines[row]);
        } else {
            text = furi_string_get_cstr(model->stream_buf);
        }
        canvas_draw_str(canvas, 0, y, text);
        y += 10;
    }

    // Footer hint
    canvas_draw_line(canvas, 0, 54, 127, 54);
    if(model->streaming) {
        canvas_draw_str(canvas, 0, 63, "AI is typing...");
    } else {
        canvas_draw_str(canvas, 0, 63, "OK:type  <:models  v^:scroll");
    }
}

static bool chat_view_input_callback(InputEvent* event, void* context) {
    View* view = context;
    ChatViewContext* ctx = view_get_context(view);
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyOk) {
            if(ctx->callback) ctx->callback(AiChatEventOpenKeyboard, ctx->callback_context);
            consumed = true;
        } else if(event->key == InputKeyLeft) {
            if(ctx->callback) ctx->callback(AiChatEventOpenModelSelect, ctx->callback_context);
            consumed = true;
        } else if(event->key == InputKeyUp) {
            with_view_model(
                view, ChatViewModel * model, { model->scroll_offset++; }, true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                view,
                ChatViewModel * model,
                {
                    if(model->scroll_offset > 0) model->scroll_offset--;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyBack) {
            if(ctx->callback) ctx->callback(AiChatEventExit, ctx->callback_context);
            consumed = true;
        }
    }
    return consumed;
}

View* ai_chat_chat_view_alloc(void) {
    View* view = view_alloc();
    view_allocate_model(view, ViewModelTypeLocking, sizeof(ChatViewModel));

    with_view_model(
        view,
        ChatViewModel * model,
        {
            model->line_count = 0;
            model->scroll_offset = 0;
            model->streaming = false;
            model->stream_buf = furi_string_alloc();
            model->status = furi_string_alloc_set("connecting...");
            model->model_name = furi_string_alloc_set("AI Chat");
        },
        true);

    ChatViewContext* ctx = malloc(sizeof(ChatViewContext));
    ctx->callback = NULL;
    ctx->callback_context = NULL;
    view_set_context(view, ctx);

    view_set_draw_callback(view, chat_view_draw_callback);
    view_set_input_callback(view, chat_view_input_callback);
    return view;
}

void ai_chat_chat_view_free(View* view) {
    if(!view) return;
    with_view_model(
        view,
        ChatViewModel * model,
        {
            for(size_t i = 0; i < model->line_count; i++) furi_string_free(model->lines[i]);
            furi_string_free(model->stream_buf);
            furi_string_free(model->status);
            furi_string_free(model->model_name);
        },
        false);
    free(view_get_context(view));
    view_free(view);
}

void ai_chat_chat_view_set_event_callback(
    View* view,
    AiChatChatViewEventCallback callback,
    void* context) {
    ChatViewContext* ctx = view_get_context(view);
    ctx->callback = callback;
    ctx->callback_context = context;
}

void ai_chat_chat_view_add_line(View* view, const char* text) {
    with_view_model(
        view, ChatViewModel * model, { chat_view_push_wrapped(model, text); }, true);
}

void ai_chat_chat_view_stream_begin(View* view, const char* speaker_prefix) {
    with_view_model(
        view,
        ChatViewModel * model,
        {
            model->streaming = true;
            furi_string_set(model->stream_buf, speaker_prefix);
        },
        true);
}

void ai_chat_chat_view_stream_append(View* view, const char* chunk) {
    with_view_model(
        view,
        ChatViewModel * model,
        {
            furi_string_cat_str(model->stream_buf, chunk);
            // Keep only the tail visible so it never overflows one row.
            size_t len = furi_string_size(model->stream_buf);
            if(len > WRAP_COLS) {
                furi_string_right(model->stream_buf, len - WRAP_COLS);
            }
        },
        true);
}

void ai_chat_chat_view_stream_end(View* view) {
    with_view_model(
        view,
        ChatViewModel * model,
        {
            model->streaming = false;
            chat_view_push_wrapped(model, furi_string_get_cstr(model->stream_buf));
            furi_string_reset(model->stream_buf);
        },
        true);
}

void ai_chat_chat_view_set_status(View* view, const char* status) {
    with_view_model(
        view, ChatViewModel * model, { furi_string_set(model->status, status); }, true);
}

void ai_chat_chat_view_set_model_name(View* view, const char* model_name) {
    with_view_model(
        view,
        ChatViewModel * model,
        { furi_string_set(model->model_name, model_name); },
        true);
}
