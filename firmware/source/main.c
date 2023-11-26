#include "api.h"
#include "pico/stdlib.h"

extern void game_pong_loop();
extern void game_pong_init();

int main() {
    cpu_init(30);
    gpu_init(30);
    ipu_init();

    gpu_set_background_color(0xFF);
    gpu_set_foreground_color(0x00);

    game_pong_init();
    cpu_run(game_pong_loop);
}