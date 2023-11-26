#include "api.h"
#include "hardware/spi.h"

#define RX_PIN    4
#define CS_PIN    5
#define SCK_PIN   6
#define TX_PIN    7
#define DC_PIN    1
#define RESET_PIN 2

static const uint8_t DISPLAY_FUNCTION_CONTROL = 0xB6;
static const uint8_t DISPLAY_OFF              = 0x28;
static const uint8_t DISPLAY_ON               = 0x29;
static const uint8_t DRIVER_TIMING_CONTROL_A  = 0xE8;
static const uint8_t DRIVER_TIMING_CONTROL_B  = 0xEA;
static const uint8_t ENABLE_3GAMMA_CONTROL    = 0xF2;
static const uint8_t FRAME_RATE_CONTROL       = 0xB1;
static const uint8_t MEMORY_ACCESS_CONTROL    = 0x36;
static const uint8_t NEGATIVE_GAMMA_CONTROL   = 0xE1;
static const uint8_t POSITIVE_GAMMA_CONTROL   = 0xE0;
static const uint8_t POWER_CONTROL_1          = 0xC0;
static const uint8_t POWER_CONTROL_2          = 0xC1;
static const uint8_t POWER_CONTROL_A          = 0xCB;
static const uint8_t POWER_CONTROL_B          = 0xCF;
static const uint8_t POWER_ON_SEQUENCE        = 0xED;
static const uint8_t PUMP_RATIO_CONTROL       = 0xF7;
static const uint8_t SET_COLUMN_ADDRESS       = 0x2A;
static const uint8_t SET_GAMMA                = 0x26;
static const uint8_t SET_PAGE_ADDRESS         = 0x2B;
static const uint8_t SET_PIXEL_FORMAT         = 0x3A;
static const uint8_t TEARING_LINE_ON          = 0x35;
static const uint8_t VCOM_CONTROL_1           = 0xC5;
static const uint8_t VCOM_CONTROL_2           = 0xC7;
static const uint8_t WAKE_UP                  = 0x11;
static const uint8_t WRITE_MEMORY             = 0x2C;

#define send(command)                                        \
    gpio_put(DC_PIN, 0);                                     \
    spi_write_blocking(spi_default, (uint8_t*) &command, 1); \
    gpio_put(DC_PIN, 1)

#define write(data, size) spi_write_blocking(spi_default, (uint8_t*) data, size)

#define write8(data)  write(&data, 1)
#define write16(data) write(&data, 2)

#define read(data, size) spi_read_blocking(spi_default, 0x01, (uint8_t*) data, size)
#define read8(data)      read(&data, 1)
#define read16(data)     read(&data, 2)

#define execute(command, data, size) \
    send(command);                   \
    write(data, size)

static inline void set_address(const uint16_t x0,
                               const uint16_t y0,
                               const uint16_t x1,
                               const uint16_t y1) {
    uint16_t sx0 = (x0 << 8) | (x0 >> 8);
    uint16_t sx1 = (x1 << 8) | (x1 >> 8);
    uint16_t sy0 = (y0 << 8) | (y0 >> 8);
    uint16_t sy1 = (y1 << 8) | (y1 >> 8);

    send(SET_COLUMN_ADDRESS);
    write16(sx0);
    write16(sx1);

    send(SET_PAGE_ADDRESS);
    write16(sy0);
    write16(sy1);
}

uint display_init(void) {
    spi_init(spi_default, 32000000);

    gpio_set_function(RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CS_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TX_PIN, GPIO_FUNC_SPI);
    gpio_put(CS_PIN, 0);

    gpio_init(DC_PIN);
    gpio_set_dir(DC_PIN, GPIO_OUT);
    gpio_put(DC_PIN, 0);

    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_OUT);
    gpio_put(RESET_PIN, 0);
    sleep_ms(50);
    gpio_put(RESET_PIN, 1);
    sleep_ms(50);

    execute("\xEF", "\x03\x80\x02", 3);    // undocumented command

    execute(POWER_CONTROL_B, "\x00\xC1\x30", 3);
    execute(POWER_ON_SEQUENCE, "\x64\x03\x12\x81", 4);
    execute(DRIVER_TIMING_CONTROL_A, "\x85\x00\x78", 3);
    execute(POWER_CONTROL_A, "\x39\x2c\x00\x34\x02", 5);
    execute(PUMP_RATIO_CONTROL, "\x20", 1);
    execute(DRIVER_TIMING_CONTROL_B, "\x00\x00", 2);
    execute(POWER_CONTROL_1, "\x23", 1);
    execute(POWER_CONTROL_2, "\x10", 1);
    execute(VCOM_CONTROL_1, "\x3e\x28", 2);
    execute(VCOM_CONTROL_2, "\x86", 1);
    execute(DISPLAY_FUNCTION_CONTROL, "\x08\x82\x27", 3);
    execute(TEARING_LINE_ON, "\x00", 1);    // v-blank Only

    execute(MEMORY_ACCESS_CONTROL, "\xE8", 1);     // rotated 90 degrees
    execute(SET_PIXEL_FORMAT, "\x55", 1);          // 16-bit colors
    execute(FRAME_RATE_CONTROL, "\x00\x1B", 2);    // 70hz

    display_clear(0x528A);
    send(WAKE_UP);
    sleep_ms(200);
    send(DISPLAY_ON);
    sleep_ms(200);
}

void display_clear(const uint16_t color) {
    uint16_t swapped_color = color << 8 | color >> 8;

    set_address(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    send(WRITE_MEMORY);

    for (int pixel = 0; pixel < DISPLAY_WIDTH * DISPLAY_HEIGHT; pixel++) {
        write16(swapped_color);
    }
}

void display_set_pixel(const uint16_t x, const uint16_t y, const uint16_t color) {
    set_address(x, y, x, y);
    send(WRITE_MEMORY);
    write16(color);
}

void display_blit(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h, uint16_t* data) {
    set_address(x, y, x + w - 1, y + h - 1);
    send(WRITE_MEMORY);
    write(data, w * h * 2);
}