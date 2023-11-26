#ifndef DRIVERS_H
#define DRIVERS_H

#include "pico/stdlib.h"

// Display

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

uint display_init(void);
void display_clear(const uint16_t color);
void display_set_pixel(const uint16_t x, const uint16_t y, const uint16_t color);
void display_blit(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h, uint16_t* data);

// GPU

#define GPU_RESOLUTION_WIDTH  160
#define GPU_RESOLUTION_HEIGHT 120

#define GPU_PALETTE_DEFAULT    0
#define GPU_PALETTE_SATURATED  1
#define GPU_PALETTE_BLEACHED   2
#define GPU_PALETTE_INVERTED   3
#define GPU_PALETTE_LIGHTER    4
#define GPU_PALETTE_DARKER     5
#define GPU_PALETTE_WARM       6
#define GPU_PALETTE_COLD       7
#define GPU_PALETTE_GRAYSCALE  8
#define GPU_PALETTE_ALL_RED    9
#define GPU_PALETTE_ALL_ORANGE 10
#define GPU_PALETTE_ALL_YELLOW 11
#define GPU_PALETTE_ALL_GREEN  12
#define GPU_PALETTE_ALL_TEAL   13
#define GPU_PALETTE_ALL_BLUE   14
#define GPU_PALETTE_ALL_PURPLE 15
#define GPU_PALETTE_ALL_PINK   16

#define GPU_PALETTE_COUNT 17

#define GPU_SMALL_CHAR_WIDTH  5
#define GPU_SMALL_CHAR_HEIGHT 7

#define GPU_PRINT_RIGHT 5000

typedef void* gpu_sheet;

void     gpu_init(const uint8_t max_fps);
void     gpu_clear();
void     gpu_set_background_color(const uint8_t color);
void     gpu_set_foreground_color(const uint8_t color);
void     gpu_set_palette(const uint8_t palette_index);
void     gpu_set_pixel(const uint16_t x, const uint16_t y, const uint8_t color);
void     gpu_blit(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h, uint8_t* data);
void     gpu_print_small(const uint16_t x, const uint16_t y, const char* text, ...);
void     gpu_sync(void);
uint64_t gpu_get_last_frame_time(void);
uint64_t gpu_get_last_busy_time(void);

// CPU

typedef void(cpu_step_function(void));

void     cpu_init(const uint8_t clock_speed);
void     cpu_run(cpu_step_function step_function);
uint64_t cpu_get_last_step_time(void);
uint64_t cpu_get_last_cycle_time(void);

// IPU

#define IPU_BUTTON_UP    0
#define IPU_BUTTON_LEFT  1
#define IPU_BUTTON_DOWN  2
#define IPU_BUTTON_RIGHT 3
#define IPU_BUTTON_BACK  4
#define IPU_BUTTON_START 5
#define IPU_BUTTON_A     6
#define IPU_BUTTON_B     7

#define IPU_BUTTON_PRESSED(button) ((ipu_get_state() >> button) & 0b1)

void    ipu_init(void);
uint8_t ipu_read(void);
uint8_t ipu_get_state(void);

#endif