#include "ai_chat_chat_view.h"
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HISTORY_LINES 64
#define WRAP_COLS 22 // chars per row at FontSecondary on a 128px wide canvas,
                      // leaves ~6px on the right for the scroll indicator

// Layout constants for the body (history) area. These must stay consistent
// with each other or rows silently draw into the footer - see the
// VISIBLE_ROWS derivation below.
#define HEADER_LINE_Y 9
#define FIRST_ROW_Y 19
#define ROW_HEIGHT 9
#define FOOTER_LINE_Y 54
#define FOOTER_TEXT_Y 63
#define SCROLL_INDICATOR_X 123

// Previously hardcoded to 6, which put the last couple of rows underneath
// (and overlapping) the footer divider/hint text. Derive it instead from
// the actual pixel geometry so the body area can never draw past
// FOOTER_LINE_Y, with a couple px of padding for font descenders.
#define VISIBLE_ROWS (((FOOTER_LINE_Y - 2) - FIRST_ROW_Y) / ROW_HEIGHT + 1)

typedef struct {
    FuriString* lines[MAX_HISTORY_LINES];
    size_t line_count;
    size_t scroll_offset; // rows scrolled up from the bottom

    bool streaming;
    FuriString* stream_buf; // "AI: partial tokens..."

    FuriString* status;
    FuriString* model_name;

    // Event callback lives in the model (not a separate context struct) so
    // that the input callback can reach it via with_view_model. The SDK's
    // View API only exposes view_set_context(), there's no getter, so we
    // avoid needing one by using the View* itself as its own context.
    AiChatChatViewEventCallback callback;
    void* callback_context;
} ChatViewModel;

static void chat_view_push_line(ChatViewModel* model, const char* text) {
    if(model->line_count == MAX_HISTORY_LINES) {
        furi_string_free(model->lines[0]);
        memmove(&model->lines[0], &model->lines[1], sizeof(FuriString*) * (MAX_HISTORY_LINES - 1));
        model->line_count--;
    }
    model->lines[model->line_count++] = furi_string_alloc_set(text);
    // Auto-scroll to the bottom whenever new content arrives, so a line that
    // wraps past the visible rows pushes the view down instead of getting
    // stuck out of sight behind whatever scroll position was left over from
    // an earlier manual scroll-up.
    model->scroll_offset = 0;
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
    canvas_draw_line(canvas, 0, HEADER_LINE_Y, 127, HEADER_LINE_Y);

    // History, bottom-anchored
    size_t total_rows = model->line_count + (model->streaming ? 1 : 0);
    size_t visible = VISIBLE_ROWS;
    size_t start = 0;
    bool can_scroll_up = false;
    if(total_rows > visible) {
        start = total_rows - visible - model->scroll_offset;
        if(start > total_rows) start = 0; // clamp on underflow
        can_scroll_up = start > 0;
    }

    int y = FIRST_ROW_Y;
    for(size_t row = start; row < total_rows && (row - start) < visible; row++) {
        const char* text;
        if(row < model->line_count) {
            text = furi_string_get_cstr(model->lines[row]);
        } else {
            text = furi_string_get_cstr(model->stream_buf);
        }
        canvas_draw_str(canvas, 0, y, text);
        y += ROW_HEIGHT;
    }

    // Move/scroll indicators, only shown while idle (not mid-stream) so they
    // don't fight with the "AI is typing..." hint below.
    if(!model->streaming) {
        if(can_scroll_up) canvas_draw_str(canvas, SCROLL_INDICATOR_X, FIRST_ROW_Y, "^");
        if(model->scroll_offset > 0)
            canvas_draw_str(canvas, SCROLL_INDICATOR_X, FIRST_ROW_Y + (VISIBLE_ROWS - 1) * ROW_HEIGHT, "v");
    }

    // Footer hint
    canvas_draw_line(canvas, 0, FOOTER_LINE_Y, 127, FOOTER_LINE_Y);
    if(model->streaming) {
        canvas_draw_str(canvas, 0, FOOTER_TEXT_Y, "AI is typing...");
    } else {
        canvas_draw_str(canvas, 0, FOOTER_TEXT_Y, "OK:chat <mdl >cfg ^v:scrl");
    }
}

static bool chat_view_input_callback(InputEvent* event, void* context) {
    // We set the View* itself as its own context (see alloc below), so
    // `context` here IS the View* - no view_get_context() call needed.
    View* view = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyOk) {
            AiChatChatViewEventCallback callback = NULL;
            void* callback_context = NULL;
            with_view_model(
                view,
                ChatViewModel * model,
                {
                    callback = model->callback;
                    callback_context = model->callback_context;
                },
                false);
            if(callback) callback(AiChatEventOpenKeyboard, callback_context);
            consumed = true;
        } else if(event->key == InputKeyLeft) {
            AiChatChatViewEventCallback callback = NULL;
            void* callback_context = NULL;
            with_view_model(
                view,
                ChatViewModel * model,
                {
                    callback = model->callback;
                    callback_context = model->callback_context;
                },
                false);
            if(callback) callback(AiChatEventOpenModelSelect, callback_context);
            consumed = true;
        } else if(event->key == InputKeyRight) {
            AiChatChatViewEventCallback callback = NULL;
            void* callback_context = NULL;
            with_view_model(
                view,
                ChatViewModel * model,
                {
                    callback = model->callback;
                    callback_context = model->callback_context;
                },
                false);
            if(callback) callback(AiChatEventOpenSettings, callback_context);
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
            AiChatChatViewEventCallback callback = NULL;
            void* callback_context = NULL;
            with_view_model(
                view,
                ChatViewModel * model,
                {
                    callback = model->callback;
                    callback_context = model->callback_context;
                },
                false);
            if(callback) callback(AiChatEventExit, callback_context);
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
            model->callback = NULL;
            model->callback_context = NULL;
        },
        true);

    // The SDK doesn't expose a view_get_context() getter, only the setter.
    // Rather than malloc a side struct we'd have no way to retrieve later,
    // use the View* itself as its own context - input_callback gets it back
    // for free as its `context` argument.
    view_set_context(view, view);

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
    view_free(view);
}

void ai_chat_chat_view_set_event_callback(
    View* view,
    AiChatChatViewEventCallback callback,
    void* context) {
    with_view_model(
        view,
        ChatViewModel * model,
        {
            model->callback = callback;
            model->callback_context = context;
        },
        false);
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
            model->scroll_offset = 0; // jump to bottom so the new reply is visible
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
