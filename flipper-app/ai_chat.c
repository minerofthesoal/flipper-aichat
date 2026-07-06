#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <gui/modules/submenu.h>
#include <notification/notification_messages.h>

#include "ai_chat_uart.h"
#include "ai_chat_protocol.h"
#include "ai_chat_chat_view.h"

#define TAG "AiChat"
#define MAX_INPUT_LEN 240
#define MAX_MODELS 16

typedef enum {
    AiChatViewChat,
    AiChatViewKeyboard,
    AiChatViewModelSelect,
} AiChatViewId;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;

    View* chat_view;
    TextInput* text_input;
    Submenu* model_submenu;

    AiChatUart* uart;

    char input_buffer[MAX_INPUT_LEN];

    FuriMutex* models_mutex;
    FuriString* models_csv; // last "MODELS|..." payload, comma separated
    char current_model[64];

    bool running;
} AiChatApp;

// ---- UART line handling (runs on the AiChatUart worker thread) ----------

static void ai_chat_handle_uart_line(const char* line, void* context) {
    AiChatApp* app = context;
    AiChatMsg msg = ai_chat_protocol_parse(line);

    switch(msg.type) {
    case AiChatMsgToken:
        ai_chat_chat_view_stream_append(app->chat_view, furi_string_get_cstr(msg.payload));
        break;
    case AiChatMsgDone:
        ai_chat_chat_view_stream_end(app->chat_view);
        notification_message(app->notifications, &sequence_blink_stop);
        break;
    case AiChatMsgError: {
        FuriString* line_str = furi_string_alloc();
        furi_string_printf(line_str, "!! %s", furi_string_get_cstr(msg.payload));
        ai_chat_chat_view_add_line(app->chat_view, furi_string_get_cstr(line_str));
        furi_string_free(line_str);
        notification_message(app->notifications, &sequence_blink_stop);
        break;
    }
    case AiChatMsgModels:
        furi_mutex_acquire(app->models_mutex, FuriWaitForever);
        furi_string_set(app->models_csv, msg.payload);
        furi_mutex_release(app->models_mutex);
        ai_chat_chat_view_add_line(app->chat_view, "(model list updated, press < )");
        break;
    case AiChatMsgInfo:
        ai_chat_chat_view_set_status(app->chat_view, furi_string_get_cstr(msg.payload));
        break;
    case AiChatMsgUnknown:
    default:
        // Surface unrecognized lines so debugging the bridge is possible
        // without a separate serial monitor.
        ai_chat_chat_view_add_line(app->chat_view, furi_string_get_cstr(msg.payload));
        break;
    }

    ai_chat_msg_free(&msg);
}

// ---- Chat view events -----------------------------------------------------

static void ai_chat_text_input_result(void* context) {
    AiChatApp* app = context;

    if(strlen(app->input_buffer) > 0) {
        FuriString* line = furi_string_alloc();
        furi_string_printf(line, "You: %s", app->input_buffer);
        ai_chat_chat_view_add_line(app->chat_view, furi_string_get_cstr(line));
        furi_string_free(line);

        FuriString* wire = ai_chat_protocol_build_chat(app->input_buffer);
        ai_chat_uart_send_line(app->uart, furi_string_get_cstr(wire));
        furi_string_free(wire);

        ai_chat_chat_view_stream_begin(app->chat_view, "AI: ");
        notification_message(app->notifications, &sequence_blink_start_cyan);
    }

    app->input_buffer[0] = '\0';
    view_dispatcher_switch_to_view(app->view_dispatcher, AiChatViewChat);
}

static uint32_t ai_chat_exit_to_chat(void* context) {
    UNUSED(context);
    return AiChatViewChat;
}

static void ai_chat_model_select_callback(void* context, uint32_t index) {
    AiChatApp* app = context;

    furi_mutex_acquire(app->models_mutex, FuriWaitForever);
    FuriString* csv = furi_string_alloc_set(app->models_csv);
    furi_mutex_release(app->models_mutex);

    // Walk the comma separated list to find entry `index`.
    size_t start = 0;
    size_t cursor = 0;
    uint32_t current = 0;
    size_t len = furi_string_size(csv);
    while(cursor <= len) {
        if(cursor == len || furi_string_get_char(csv, cursor) == ',') {
            if(current == index) {
                FuriString* name = furi_string_alloc();
                furi_string_set_n(name, csv, start, cursor - start);
                strncpy(app->current_model, furi_string_get_cstr(name), sizeof(app->current_model) - 1);
                ai_chat_chat_view_set_model_name(app->chat_view, app->current_model);

                FuriString* wire = ai_chat_protocol_build_select_model(app->current_model);
                ai_chat_uart_send_line(app->uart, furi_string_get_cstr(wire));

                furi_string_free(wire);
                furi_string_free(name);
                break;
            }
            current++;
            start = cursor + 1;
        }
        cursor++;
    }
    furi_string_free(csv);

    view_dispatcher_switch_to_view(app->view_dispatcher, AiChatViewChat);
}

static void ai_chat_open_model_select(AiChatApp* app) {
    submenu_reset(app->model_submenu);
    submenu_set_header(app->model_submenu, "Select model");

    furi_mutex_acquire(app->models_mutex, FuriWaitForever);
    FuriString* csv = furi_string_alloc_set(app->models_csv);
    furi_mutex_release(app->models_mutex);

    if(furi_string_size(csv) == 0) {
        submenu_add_item(app->model_submenu, "(no models yet, sent request)", 0, NULL, app);
        ai_chat_uart_send_line(app->uart, "LISTMODELS");
    } else {
        size_t start = 0;
        size_t len = furi_string_size(csv);
        uint32_t idx = 0;
        for(size_t cursor = 0; cursor <= len; cursor++) {
            if(cursor == len || furi_string_get_char(csv, cursor) == ',') {
                if(cursor > start) {
                    FuriString* name = furi_string_alloc();
                    furi_string_set_n(name, csv, start, cursor - start);
                    submenu_add_item(
                        app->model_submenu,
                        furi_string_get_cstr(name),
                        idx,
                        ai_chat_model_select_callback,
                        app);
                    furi_string_free(name);
                    idx++;
                }
                start = cursor + 1;
            }
        }
    }
    furi_string_free(csv);

    view_dispatcher_switch_to_view(app->view_dispatcher, AiChatViewModelSelect);
}

static void ai_chat_view_event_callback(AiChatChatViewEvent event, void* context) {
    AiChatApp* app = context;
    switch(event) {
    case AiChatEventOpenKeyboard:
        text_input_set_header_text(app->text_input, "Send message");
        view_dispatcher_switch_to_view(app->view_dispatcher, AiChatViewKeyboard);
        break;
    case AiChatEventOpenModelSelect:
        ai_chat_open_model_select(app);
        break;
    case AiChatEventExit:
        app->running = false;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    }
}

// ---- App lifecycle ---------------------------------------------------------

static AiChatApp* ai_chat_app_alloc(void) {
    AiChatApp* app = malloc(sizeof(AiChatApp));
    memset(app, 0, sizeof(AiChatApp));

    app->models_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->models_csv = furi_string_alloc();
    strncpy(app->current_model, "auto", sizeof(app->current_model) - 1);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->chat_view = ai_chat_chat_view_alloc();
    ai_chat_chat_view_set_event_callback(app->chat_view, ai_chat_view_event_callback, app);
    view_dispatcher_add_view(app->view_dispatcher, AiChatViewChat, app->chat_view);

    app->text_input = text_input_alloc();
    text_input_set_result_callback(
        app->text_input, ai_chat_text_input_result, app, app->input_buffer, MAX_INPUT_LEN, true);
    view_set_previous_callback(text_input_get_view(app->text_input), ai_chat_exit_to_chat);
    view_dispatcher_add_view(
        app->view_dispatcher, AiChatViewKeyboard, text_input_get_view(app->text_input));

    app->model_submenu = submenu_alloc();
    view_set_previous_callback(submenu_get_view(app->model_submenu), ai_chat_exit_to_chat);
    view_dispatcher_add_view(
        app->view_dispatcher, AiChatViewModelSelect, submenu_get_view(app->model_submenu));

    app->uart = ai_chat_uart_alloc(ai_chat_handle_uart_line, app);
    ai_chat_chat_view_add_line(app->chat_view, "Booting bridge link...");
    ai_chat_uart_send_line(app->uart, "LISTMODELS");

    app->running = true;
    return app;
}

static void ai_chat_app_free(AiChatApp* app) {
    ai_chat_uart_free(app->uart);

    view_dispatcher_remove_view(app->view_dispatcher, AiChatViewModelSelect);
    submenu_free(app->model_submenu);

    view_dispatcher_remove_view(app->view_dispatcher, AiChatViewKeyboard);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, AiChatViewChat);
    ai_chat_chat_view_free(app->chat_view);

    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    furi_string_free(app->models_csv);
    furi_mutex_free(app->models_mutex);
    free(app);
}

int32_t ai_chat_app(void* p) {
    UNUSED(p);
    AiChatApp* app = ai_chat_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, AiChatViewChat);
    view_dispatcher_run(app->view_dispatcher);
    ai_chat_app_free(app);
    return 0;
}
