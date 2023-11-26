#include "api.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include <stdarg.h>
#include <stdio.h>

#define FRAMEBUFFER_X           (DISPLAY_WIDTH - GPU_RESOLUTION_WIDTH) * 0.5
#define FRAMEBUFFER_Y           (DISPLAY_HEIGHT - GPU_RESOLUTION_HEIGHT) * 0.5
#define FRAMEBUFFER_CELL_WIDTH  32
#define FRAMEBUFFER_CELL_HEIGHT 24
#define FRAMEBUFFER_CELL_SIZE   FRAMEBUFFER_CELL_WIDTH* FRAMEBUFFER_CELL_HEIGHT
#define FRAMEBUFFER_COLUMNS     GPU_RESOLUTION_WIDTH / FRAMEBUFFER_CELL_WIDTH
#define FRAMEBUFFER_ROWS        GPU_RESOLUTION_HEIGHT / FRAMEBUFFER_CELL_HEIGHT

#define SMALL_FONT_COLUMNS SMALL_FONT_WIDTH / GPU_SMALL_CHAR_WIDTH
#define SMALL_FONT_ROWS    SMALL_FONT_HEIGHT / GPU_SMALL_CHAR_HEIGHT

#define COMMAND_CLEAR                0
#define COMMAND_SET_BACKGROUND_COLOR 1
#define COMMAND_SET_FOREGROUND_COLOR 2
#define COMMAND_SET_PALETTE          3
#define COMMAND_SET_X                4
#define COMMAND_SET_Y                5
#define COMMAND_SET_W                6
#define COMMAND_SET_H                7
#define COMMAND_SET_PIXEL            8
#define COMMAND_BLIT                 9
#define COMMAND_PRINT_SMALL          10
#define COMMAND_SYNC                 11

#define PRINT_BUFFER_CAPACITY   16
#define PRINT_BUFFER_MAX_LENGTH 64
#define PRINT_RIGHT_START       GPU_PRINT_RIGHT - 1000

#define SMALL_FONT_WIDTH  80
#define SMALL_FONT_HEIGHT 56

extern uint16_t img_small_font[];

static struct {
        struct {
                uint16_t x;
                uint16_t y;
        } coords;

        struct {
                uint16_t w;
                uint16_t h;
        } size;

        struct {
                uint8_t background;
                uint8_t foreground;
        } colors;

        struct {
                uint64_t min_frame;
                uint64_t last_sync;
                uint64_t last_frame;
                uint64_t last_busy;
        } time;

        struct {
                uint8_t  active_index;
                uint16_t colors[GPU_PALETTE_COUNT][256];
        } palette;

        struct {
                char     buffers[PRINT_BUFFER_CAPACITY][PRINT_BUFFER_MAX_LENGTH];
                uint16_t buffer_length[PRINT_BUFFER_CAPACITY];
                uint16_t buffer_index;
        } text;

        struct {
                uint16_t data[FRAMEBUFFER_CELL_SIZE];
                bool     is_dirty;
                bool     is_clear;
        } framebuffer[FRAMEBUFFER_ROWS][FRAMEBUFFER_COLUMNS];

        queue_t commands;
} gpu;

typedef struct {
        float r, g, b;
} rgb_color;

static rgb_color from_display_color(uint16_t display_color) {
    display_color = (display_color << 8) | (display_color >> 8);

    return (rgb_color) {
        (float) (display_color >> 11) / 31.0f,
        (float) ((display_color >> 5) & 0b111111) / 63.0f,
        (float) (display_color & 0b11111) / 31.0f,
    };
}

static uint16_t to_display_color(rgb_color color) {
    if (color.r < 0) {
        color.r = 0;
    }

    if (color.r > 1.0f) {
        color.r = 1.0f;
    }

    if (color.g < 0) {
        color.g = 0;
    }

    if (color.g > 1.0f) {
        color.g = 1.0f;
    }

    if (color.b < 0) {
        color.b = 0;
    }

    if (color.b > 1.0f) {
        color.b = 1.0f;
    }

    uint16_t display_color = ((uint16_t) (color.r * 31) << 11) | ((uint16_t) (color.g * 63) << 5) | (uint16_t) (color.b * 31);

    return (display_color << 8) | (display_color >> 8);
}

static void build_default_palettes() {
    uint16_t color, red, green, blue;

    const uint16_t reds[8]   = {0, 4, 8, 12, 16, 20, 24, 31};
    const uint16_t greens[8] = {0, 8, 16, 24, 32, 40, 48, 63};
    const uint16_t blues[4]  = {0, 8, 16, 31};

    // RRR GGG BB > RRRRR GGGGGG BBBBB
    for (uint8_t palette_index = 0; palette_index < GPU_PALETTE_COUNT; palette_index++) {
        for (uint16_t color_index = 0; color_index < 256; color_index++) {
            red   = reds[color_index >> 5];
            green = greens[(color_index >> 2) & 0b111];
            blue  = blues[color_index & 0b11];

            color = (red << 11) | (green << 5) | blue;

            gpu.palette.colors[palette_index][color_index] = (color << 8) | (color >> 8);
        }
    }
}

static void saturate_palette(const uint8_t palette_index, float amount) {
    rgb_color color;
    float     gray;

    amount -= 1.0;

    for (uint16_t color_index = 0; color_index < 256; color_index++) {
        color = from_display_color(gpu.palette.colors[palette_index][color_index]);
        gray  = (color.r + color.g + color.b) / 3.0f;

        if (color.r < gray) {
            color.r -= (color.r * amount);

            if (color.r > gray) {
                color.r = gray;
            }
        } else {
            color.r += (color.r * amount);

            if (color.r < gray) {
                color.r = gray;
            }
        }

        if (color.g < gray) {
            color.g -= (color.g * amount);

            if (color.g > gray) {
                color.g = gray;
            }
        } else {
            color.g += (color.g * amount);

            if (color.g < gray) {
                color.g = gray;
            }
        }

        if (color.b < gray) {
            color.b -= (color.b * amount);

            if (color.b > gray) {
                color.b = gray;
            }
        } else {
            color.b += (color.b * amount);

            if (color.b < gray) {
                color.b = gray;
            }
        }

        gpu.palette.colors[palette_index][color_index] = to_display_color(color);
    }
}

static void mix_palette(const uint8_t palette_index, const rgb_color mix_color, const float amount) {
    rgb_color color;

    for (uint16_t color_index = 0; color_index < 256; color_index++) {
        color = from_display_color(gpu.palette.colors[palette_index][color_index]);

        color.r = (color.r * (1 - amount)) + (mix_color.r * amount);
        color.g = (color.g * (1 - amount)) + (mix_color.g * amount);
        color.b = (color.b * (1 - amount)) + (mix_color.b * amount);

        gpu.palette.colors[palette_index][color_index] = to_display_color(color);
    }
}

static void color_grade_palette(const uint8_t palette_index, const rgb_color stops[], const uint8_t stops_count) {
    rgb_color grade_colors[256];
    uint8_t   grade_color_index = 0;
    uint8_t   colors_per_stop   = 256 / stops_count;
    rgb_color current_stop, next_stop, stop_step;

    for (uint8_t stop_index = 0; stop_index < stops_count; stop_index++) {
        current_stop = stops[stop_index];
        next_stop    = stops[stop_index + 1];

        stop_step.r = (next_stop.r - current_stop.r) / colors_per_stop;
        stop_step.g = (next_stop.g - current_stop.g) / colors_per_stop;
        stop_step.b = (next_stop.b - current_stop.b) / colors_per_stop;

        for (uint8_t stop_color_index = 0; stop_color_index < colors_per_stop; stop_color_index++) {
            grade_colors[grade_color_index].r = current_stop.r + (stop_color_index * stop_step.r);
            grade_colors[grade_color_index].g = current_stop.g + (stop_color_index * stop_step.g);
            grade_colors[grade_color_index].b = current_stop.b + (stop_color_index * stop_step.b);
            grade_color_index++;
        }
    }

    rgb_color color;
    float     previous_gray = 0, gray;

    for (uint16_t color_index = 0; color_index < 256; color_index++) {
        color = from_display_color(gpu.palette.colors[palette_index][color_index]);
        gray  = (color.r + color.g + color.b) / 3.0f;

        if (gray == previous_gray) {
            gray *= 1.1f;
        }

        previous_gray     = gray;
        grade_color_index = gray * 255;

        gpu.palette.colors[palette_index][color_index] = to_display_color(grade_colors[grade_color_index]);
    }
}

static void invert_palette(const uint8_t palette_index) {
    rgb_color color;

    for (uint16_t color_index = 0; color_index < 256; color_index++) {
        color = from_display_color(gpu.palette.colors[palette_index][color_index]);

        color.r = 1 - color.r;
        color.g = 1 - color.g;
        color.b = 1 - color.b;

        gpu.palette.colors[palette_index][color_index] = to_display_color(color);
    }
}

static void build_palettes() {
    build_default_palettes();

    mix_palette(GPU_PALETTE_LIGHTER, (rgb_color) {1.0, 1.0, 1.0}, 0.3);
    mix_palette(GPU_PALETTE_DARKER, (rgb_color) {0.0, 0.0, 0.0}, 0.3);

    saturate_palette(GPU_PALETTE_SATURATED, 1.6);
    saturate_palette(GPU_PALETTE_BLEACHED, 0.8);

    invert_palette(GPU_PALETTE_INVERTED);

    color_grade_palette(
        GPU_PALETTE_WARM,
        (rgb_color[]) {
            {0.4f, 0.0f, 0.0f},
            {0.8f, 0.2f, 0.0f},
            {0.8f, 0.4f, 0.2f},
            {0.8f, 0.8f, 0.4f},
            {1.0f, 0.8f, 0.8f},
    },
        4);

    color_grade_palette(
        GPU_PALETTE_COLD,
        (rgb_color[]) {
            {0.0f, 0.0f, 0.4f},
            {0.0f, 0.2f, 0.8f},
            {0.2f, 0.4f, 0.8f},
            {0.4f, 0.8f, 0.8f},
            {0.8f, 0.8f, 1.0f},
    },
        4);

    color_grade_palette(GPU_PALETTE_ALL_RED, (rgb_color[]) {
                                                 {0.4f, 0.0f, 0.0f},
                                                 {0.8f, 0.2f, 0.2f},
                                                 {1.0f, 0.8f, 0.8f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_GREEN, (rgb_color[]) {
                                                   {0.0f, 0.4f, 0.0f},
                                                   {0.2f, 0.8f, 0.2f},
                                                   {0.8f, 1.0f, 0.8f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_BLUE, (rgb_color[]) {
                                                  {0.0f, 0.0f, 0.4f},
                                                  {0.2f, 0.2f, 0.8f},
                                                  {0.8f, 0.8f, 1.0f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_YELLOW, (rgb_color[]) {
                                                    {0.4f, 0.4f, 0.0f},
                                                    {0.8f, 0.8f, 0.2f},
                                                    {1.0f, 1.0f, 0.8f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_PURPLE, (rgb_color[]) {
                                                    {0.4f, 0.0f, 0.4f},
                                                    {0.8f, 0.2f, 0.8f},
                                                    {1.0f, 0.8f, 1.0f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_ORANGE, (rgb_color[]) {
                                                    {0.4f, 0.2f, 0.0f},
                                                    {0.8f, 0.4f, 0.2f},
                                                    {1.0f, 0.9f, 0.8f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_PINK, (rgb_color[]) {
                                                  {0.4f, 0.2f, 0.2f},
                                                  {0.8f, 0.4f, 0.4f},
                                                  {1.0f, 0.8f, 0.8f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_ALL_TEAL, (rgb_color[]) {
                                                  {0.0f, 0.4f, 0.4f},
                                                  {0.2f, 0.8f, 0.8f},
                                                  {0.8f, 1.0f, 1.0f},
    },
                        2);

    color_grade_palette(GPU_PALETTE_GRAYSCALE, (rgb_color[]) {
                                                   {0.0f, 0.0f, 0.0f},
                                                   {0.5f, 0.5f, 0.5f},
                                                   {1.0f, 1.0f, 1.0f},
    },
                        2);
}

static inline void push_command(const int command, const int param) {
    queue_add_blocking(&gpu.commands, &command);
    queue_add_blocking(&gpu.commands, &param);
}

void gpu_clear() {
    push_command(COMMAND_CLEAR, 0);
}

void gpu_set_background_color(const uint8_t color) {
    push_command(COMMAND_SET_BACKGROUND_COLOR, color);
}

void gpu_set_palette(const uint8_t palette_index) {
    push_command(COMMAND_SET_PALETTE, palette_index);
}

void gpu_set_pixel(const uint16_t x, const uint16_t y, uint8_t color) {
    push_command(COMMAND_SET_X, x);
    push_command(COMMAND_SET_Y, y);
    push_command(COMMAND_SET_PIXEL, color);
}

void gpu_blit(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h, uint8_t* data) {
    push_command(COMMAND_SET_X, x);
    push_command(COMMAND_SET_Y, y);
    push_command(COMMAND_SET_W, w);
    push_command(COMMAND_SET_H, h);
    push_command(COMMAND_BLIT, (intptr_t) data);
}

void gpu_print_small(const uint16_t x, const uint16_t y, const char* text, ...) {
    if (gpu.text.buffer_index >= PRINT_BUFFER_CAPACITY) {
        return;
    }

    uint16_t print_index = gpu.text.buffer_index++;

    va_list list;
    va_start(list, text);
    gpu.text.buffer_length[print_index] = vsprintf(gpu.text.buffers[print_index], text, list);
    va_end(list);

    if (x < PRINT_RIGHT_START) {
        push_command(COMMAND_SET_X, x);
    } else {
        push_command(COMMAND_SET_X, GPU_RESOLUTION_WIDTH - (gpu.text.buffer_length[print_index] * (GPU_SMALL_CHAR_WIDTH + 1)) - (GPU_PRINT_RIGHT - x));
    }

    push_command(COMMAND_SET_Y, y);
    push_command(COMMAND_PRINT_SMALL, print_index);
}

void gpu_sync() {
    push_command(COMMAND_SYNC, 0);
}

void gpu_set_foreground_color(const uint8_t color) {
    push_command(COMMAND_SET_FOREGROUND_COLOR, color);
}

uint64_t gpu_get_last_frame_time(void) {
    return gpu.time.last_frame;
}

uint64_t gpu_get_last_busy_time(void) {
    return gpu.time.last_busy;
}

void gpu_core() {
    int      command, parameter, row, column, pixel_index, cell_x, cell_y;
    uint64_t command_start, frame_start, frame_end, frame_busy_time = 0;
    uint8_t* data;
    uint16_t font_x, font_y, pixel_x, pixel_y, blit_x, blit_y;
    uint8_t  current_char, last_clear_color = 0;

    for (;;) {
        queue_remove_blocking(&gpu.commands, &command);
        queue_remove_blocking(&gpu.commands, &parameter);

        command_start = time_us_64();

        switch (command) {
            case COMMAND_CLEAR:
                for (row = 0; row < FRAMEBUFFER_ROWS; row++) {
                    for (column = 0; column < FRAMEBUFFER_COLUMNS; column++) {
                        if (gpu.framebuffer[row][column].is_clear && (last_clear_color == gpu.colors.background)) {
                            continue;
                        }

                        for (pixel_index = 0; pixel_index < FRAMEBUFFER_CELL_SIZE; pixel_index++) {
                            gpu.framebuffer[row][column].data[pixel_index] = gpu.palette.colors[gpu.palette.active_index][gpu.colors.background];
                        }

                        gpu.framebuffer[row][column].is_clear = true;
                        gpu.framebuffer[row][column].is_dirty = true;
                    }
                }

                last_clear_color = gpu.colors.background;
                break;

            case COMMAND_SET_BACKGROUND_COLOR:
                gpu.colors.background = parameter;
                break;

            case COMMAND_SET_FOREGROUND_COLOR:
                gpu.colors.foreground = parameter;
                break;

            case COMMAND_SET_PALETTE:
                if (parameter < GPU_PALETTE_COUNT) {
                    gpu.palette.active_index = (uint8_t) parameter;
                }

                break;

            case COMMAND_SET_X:
                gpu.coords.x = parameter;
                break;

            case COMMAND_SET_Y:
                gpu.coords.y = parameter;
                break;

            case COMMAND_SET_W:
                gpu.size.w = parameter;
                break;

            case COMMAND_SET_H:
                gpu.size.h = parameter;
                break;

            case COMMAND_SET_PIXEL:
                if ((gpu.coords.x < GPU_RESOLUTION_WIDTH) && (gpu.coords.y < GPU_RESOLUTION_HEIGHT)) {
                    row    = gpu.coords.y / FRAMEBUFFER_CELL_HEIGHT;
                    column = gpu.coords.x / FRAMEBUFFER_CELL_WIDTH;
                    cell_y = gpu.coords.y % FRAMEBUFFER_CELL_HEIGHT;
                    cell_x = gpu.coords.x % FRAMEBUFFER_CELL_WIDTH;

                    gpu.framebuffer[row][column].data[(cell_y * FRAMEBUFFER_CELL_WIDTH) + cell_x] =
                        gpu.palette.colors[gpu.palette.active_index][(uint8_t) parameter];

                    gpu.framebuffer[row][column].is_dirty = true;
                    gpu.framebuffer[row][column].is_clear = false;
                }

                break;

            case COMMAND_BLIT:
                data = (uint8_t*) parameter;

                for (uint16_t blit_y = 0; blit_y < gpu.size.h && blit_y + gpu.coords.y < GPU_RESOLUTION_HEIGHT; blit_y++) {
                    for (uint16_t blit_x = 0; blit_x < gpu.size.w && blit_x + gpu.coords.x < GPU_RESOLUTION_WIDTH; blit_x++) {
                        if (data[(blit_y * gpu.size.w) + blit_x] != 0) {
                            row    = (gpu.coords.y + blit_y) / FRAMEBUFFER_CELL_HEIGHT;
                            column = (gpu.coords.x + blit_x) / FRAMEBUFFER_CELL_WIDTH;
                            cell_y = (gpu.coords.y + blit_y) % FRAMEBUFFER_CELL_HEIGHT;
                            cell_x = (gpu.coords.x + blit_x) % FRAMEBUFFER_CELL_WIDTH;

                            gpu.framebuffer[row][column].data[(cell_y * FRAMEBUFFER_CELL_WIDTH) + (cell_x)] =
                                gpu.palette.colors[gpu.palette.active_index][data[(blit_y * gpu.size.w) + blit_x]];

                            gpu.framebuffer[row][column].is_dirty = true;
                            gpu.framebuffer[row][column].is_clear = false;
                        }
                    }
                }

                break;

            case COMMAND_PRINT_SMALL:
                for (uint8_t char_index = 0; char_index < gpu.text.buffer_length[parameter]; char_index++) {
                    current_char = gpu.text.buffers[parameter][char_index];

                    if (current_char > 127) {
                        continue;
                    }

                    font_y = (current_char / (SMALL_FONT_COLUMNS)) * GPU_SMALL_CHAR_HEIGHT;
                    blit_y = gpu.coords.y;

                    for (uint8_t pixel_y = 0; pixel_y < GPU_SMALL_CHAR_HEIGHT && blit_y < GPU_RESOLUTION_HEIGHT; pixel_y++) {
                        font_x = (current_char % (SMALL_FONT_COLUMNS)) * GPU_SMALL_CHAR_WIDTH;
                        blit_x = gpu.coords.x + (char_index * (GPU_SMALL_CHAR_WIDTH + 1));

                        for (uint8_t pixel_x = 0; pixel_x < GPU_SMALL_CHAR_WIDTH && blit_x < GPU_RESOLUTION_WIDTH; pixel_x++) {
                            if (img_small_font[(font_y * SMALL_FONT_WIDTH) + font_x] != 0) {
                                row    = blit_y / FRAMEBUFFER_CELL_HEIGHT;
                                column = blit_x / FRAMEBUFFER_CELL_WIDTH;
                                cell_y = blit_y % FRAMEBUFFER_CELL_HEIGHT;
                                cell_x = blit_x % FRAMEBUFFER_CELL_WIDTH;

                                gpu.framebuffer[row][column].data[(cell_y * FRAMEBUFFER_CELL_WIDTH) + cell_x] = gpu.palette.colors[gpu.palette.active_index][gpu.colors.foreground];

                                gpu.framebuffer[row][column].is_dirty = true;
                                gpu.framebuffer[row][column].is_clear = false;
                            }

                            font_x++;
                            blit_x++;
                        }

                        font_y++;
                        blit_y++;
                    }
                }

                break;

            case COMMAND_SYNC:
                frame_start = time_us_64();

                if (gpu.time.last_sync == 0) {
                    gpu.time.last_sync = frame_start - gpu.time.min_frame;
                }

                if (frame_start - gpu.time.last_sync < gpu.time.min_frame) {
                    break;
                }

                for (row = 0; row < FRAMEBUFFER_ROWS; row++) {
                    for (column = 0; column < FRAMEBUFFER_COLUMNS; column++) {
                        if (!gpu.framebuffer[row][column].is_dirty) {
                            continue;
                        }

                        display_blit(
                            FRAMEBUFFER_X + (column * FRAMEBUFFER_CELL_WIDTH),
                            FRAMEBUFFER_Y + (row * FRAMEBUFFER_CELL_HEIGHT),
                            FRAMEBUFFER_CELL_WIDTH, FRAMEBUFFER_CELL_HEIGHT,
                            gpu.framebuffer[row][column].data);

                        gpu.framebuffer[row][column].is_dirty = false;
                    }
                }

                frame_end             = time_us_64();
                gpu.time.last_frame   = frame_end - gpu.time.last_sync;
                gpu.time.last_busy    = frame_busy_time + (frame_end - frame_start);
                gpu.time.last_sync    = frame_start;
                frame_busy_time       = 0;
                gpu.text.buffer_index = 0;

                break;
        }

        if (command != COMMAND_SYNC) {
            frame_busy_time += time_us_64() - command_start;
        }
    }
}

void gpu_init(const uint8_t max_fps) {
    gpu.coords.x             = 0;
    gpu.coords.y             = 0;
    gpu.size.w               = 0;
    gpu.size.h               = 0;
    gpu.colors.background    = 0;
    gpu.colors.foreground    = 255;
    gpu.time.min_frame       = 1000000 / (uint64_t) max_fps;
    gpu.time.last_sync       = 0;
    gpu.time.last_frame      = gpu.time.min_frame;
    gpu.time.last_busy       = 0;
    gpu.palette.active_index = 0;
    gpu.text.buffer_index    = 0;

    for (int row = 0; row < FRAMEBUFFER_ROWS; row++) {
        for (int column = 0; column < FRAMEBUFFER_COLUMNS; column++) {
            gpu.framebuffer[row][column].is_clear = false;
            gpu.framebuffer[row][column].is_dirty = true;
        }
    }

    build_palettes();
    queue_init_with_spinlock(&gpu.commands, sizeof(int), 1000, 1);
    display_init();
    multicore_launch_core1(gpu_core);
    gpu_clear();
}