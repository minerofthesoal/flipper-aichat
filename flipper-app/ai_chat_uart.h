#pragma once

#include <furi.h>
#include <furi_hal_serial.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AI_CHAT_UART_BAUD 115200
#define AI_CHAT_UART_RX_BUF_SIZE 2048

typedef struct AiChatUart AiChatUart;

// Called from the UART worker thread whenever a full newline-terminated
// line has been received. `line` is owned by the caller of the callback
// (do not free/store the pointer, copy it if you need to keep it).
typedef void (*AiChatUartLineCallback)(const char* line, void* context);

AiChatUart* ai_chat_uart_alloc(AiChatUartLineCallback callback, void* context);

void ai_chat_uart_free(AiChatUart* uart);

// Sends a raw string followed by a newline to the devboard.
void ai_chat_uart_send_line(AiChatUart* uart, const char* line);

#ifdef __cplusplus
}
#endif
