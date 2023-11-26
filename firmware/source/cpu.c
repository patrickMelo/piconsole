#include "api.h"
#include "pico/stdlib.h"

static struct {
        struct {
                uint64_t min_cycle;
                uint64_t last_cycle;
                uint64_t last_step;
        } time;

} cpu;

void cpu_init(const uint8_t clock_speed) {
    cpu.time.min_cycle  = 1000000 / clock_speed;
    cpu.time.last_cycle = cpu.time.min_cycle;
    cpu.time.last_step  = cpu.time.last_cycle;
}

void cpu_run(cpu_step_function step_function) {
    uint64_t step_start, step_time;

    for (;;) {
        step_start = time_us_64();
        step_function();
        cpu.time.last_step = time_us_64() - step_start;

        if (cpu.time.last_step < cpu.time.min_cycle) {
            sleep_us(cpu.time.min_cycle - cpu.time.last_step);
        }

        cpu.time.last_cycle = time_us_64() - step_start;
    }
}
