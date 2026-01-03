#include "pico/stdlib.h"
#include "tusb.h"

#define CHUNK 256
uint8_t buf[CHUNK];

int main() {
    stdio_init_all();
    for (int i = 0; i < CHUNK; i++)
        buf[i] = (uint8_t)i;

    while (true) {
        tud_task();
        if (tud_cdc_connected()) {
            tud_cdc_write(buf, CHUNK);
            tud_cdc_write_flush();
        }

        // tud_task();
        //
        // if (tud_cdc_connected()) {
        //     tud_cdc_write_str("hello\n");
        //     tud_cdc_write_flush();
        // }
    }
}
