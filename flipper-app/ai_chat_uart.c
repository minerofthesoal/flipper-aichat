#include "ai_chat_uart.h"
#include <furi_hal.h>
#include <stdlib.h>
#include <string.h>

struct AiChatUart {
    FuriHalSerialHandle* serial_handle;
    FuriThread* worker_thread;
    FuriStreamBuffer* rx_stream;
    AiChatUartLineCallback callback;
    void* callback_context;
    volatile bool should_run;
};

// Pulled by furi_hal_serial async RX ISR context - keep this fast.
static void ai_chat_uart_on_rx_byte(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    UNUSED(handle);
    AiChatUart* uart = context;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(uart->rx_stream, &byte, 1, 0);
    }
}

static int32_t ai_chat_uart_worker(void* context) {
    AiChatUart* uart = context;
    char line_buf[AI_CHAT_UART_RX_BUF_SIZE];
    size_t line_len = 0;

    while(uart->should_run) {
        uint8_t byte;
        size_t got = furi_stream_buffer_receive(uart->rx_stream, &byte, 1, 100);
        if(got == 0) continue;

        if(byte == '\n') {
            line_buf[line_len] = '\0';
            if(line_len > 0 && uart->callback) {
                // Strip trailing \r if present
                if(line_buf[line_len - 1] == '\r') {
                    line_buf[line_len - 1] = '\0';
                }
                uart->callback(line_buf, uart->callback_context);
            }
            line_len = 0;
        } else if(line_len < AI_CHAT_UART_RX_BUF_SIZE - 1) {
            line_buf[line_len++] = (char)byte;
        } else {
            // Overflow guard: drop the line, keep the bridge alive.
            line_len = 0;
        }
    }
    return 0;
}

AiChatUart* ai_chat_uart_alloc(AiChatUartLineCallback callback, void* context) {
    AiChatUart* uart = malloc(sizeof(AiChatUart));
    uart->callback = callback;
    uart->callback_context = context;
    uart->should_run = true;
    uart->rx_stream = furi_stream_buffer_alloc(AI_CHAT_UART_RX_BUF_SIZE, 1);

    // USART on GPIO 13(TX)/14(RX) - matches the WiFi Devboard header pinout.
    uart->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(uart->serial_handle);
    furi_hal_serial_init(uart->serial_handle, AI_CHAT_UART_BAUD);
    furi_hal_serial_async_rx_start(uart->serial_handle, ai_chat_uart_on_rx_byte, uart, false);

    uart->worker_thread = furi_thread_alloc_ex("AiChatUartWorker", 1024, ai_chat_uart_worker, uart);
    furi_thread_start(uart->worker_thread);

    return uart;
}

void ai_chat_uart_free(AiChatUart* uart) {
    if(!uart) return;
    uart->should_run = false;
    furi_thread_join(uart->worker_thread);
    furi_thread_free(uart->worker_thread);

    furi_hal_serial_async_rx_stop(uart->serial_handle);
    furi_hal_serial_deinit(uart->serial_handle);
    furi_hal_serial_control_release(uart->serial_handle);

    furi_stream_buffer_free(uart->rx_stream);
    free(uart);
}

void ai_chat_uart_send_line(AiChatUart* uart, const char* line) {
    if(!uart || !line) return;
    furi_hal_serial_tx(uart->serial_handle, (const uint8_t*)line, strlen(line));
    const uint8_t nl = '\n';
    furi_hal_serial_tx(uart->serial_handle, &nl, 1);
}
