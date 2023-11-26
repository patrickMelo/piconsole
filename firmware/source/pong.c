#include "api.h"
#include "pico/stdlib.h"

#define PONG_BALL_WIDTH  8
#define PONG_BALL_HEIGHT 8

#define PONG_BAR_WIDTH  24
#define PONG_BAR_HEIGHT 4

extern uint8_t img_pong_ball[];
extern uint8_t img_pong_bar[];

#define MARGIN_SIZE                5
#define MIN_BALL_X                 0
#define MIN_BALL_Y                 MARGIN_SIZE + MARGIN_SIZE + GPU_SMALL_CHAR_HEIGHT
#define MAX_BALL_X                 GPU_RESOLUTION_WIDTH - PONG_BALL_WIDTH
#define MAX_BALL_Y                 GPU_RESOLUTION_HEIGHT - PONG_BALL_HEIGHT
#define MIN_BALL_SPEED             2
#define MAX_BALL_SPEED             10
#define PLAYER_SPEED               2
#define MIN_PLAYER_X               0
#define MAX_PLAYER_X               GPU_RESOLUTION_WIDTH - PONG_BAR_WIDTH
#define BALL_SPEED_INCREASE_POINTS 10
#define START_SCORE                10

#define STATE_IN_GAME 0
#define STATE_WON     1
#define STATE_LOST    2

static struct {
        struct {
                uint16_t x;
                uint16_t y;
                int16_t  x_direction;
                int16_t  y_direction;
                uint16_t speed;
                uint16_t next_speed_increase_at;
        } ball;

        struct {
                uint16_t x;
                uint16_t y;
        } player;

        uint8_t score;
        uint8_t state;
} pong;

void game_pong_init() {
    pong.score                       = START_SCORE;
    pong.ball.x                      = MIN_BALL_X + (time_us_64() % (MAX_BALL_X - MIN_BALL_X));
    pong.ball.y                      = MIN_BALL_Y + (time_us_64() % (MAX_BALL_Y - MIN_BALL_Y));
    pong.ball.x_direction            = 1;
    pong.ball.y_direction            = 1;
    pong.ball.speed                  = MIN_BALL_SPEED;
    pong.ball.next_speed_increase_at = MIN_BALL_SPEED + BALL_SPEED_INCREASE_POINTS;
    pong.player.y                    = GPU_RESOLUTION_HEIGHT - (PONG_BAR_HEIGHT + MARGIN_SIZE);
    pong.player.x                    = MIN_PLAYER_X + (((MAX_PLAYER_X - MIN_PLAYER_X) - PONG_BAR_WIDTH) / 2);
    pong.state                       = STATE_IN_GAME;
}

void update_player() {
    ipu_read();

    if (IPU_BUTTON_PRESSED(IPU_BUTTON_LEFT)) {
        pong.player.x -= PLAYER_SPEED;

        if (pong.player.x < MIN_PLAYER_X) {
            pong.player.x = MIN_PLAYER_X;
        }
    }

    if (IPU_BUTTON_PRESSED(IPU_BUTTON_RIGHT)) {
        pong.player.x += PLAYER_SPEED;

        if (pong.player.x > MAX_PLAYER_X) {
            pong.player.x = MAX_PLAYER_X;
        }
    }
}

void update_ball() {
    pong.ball.x += pong.ball.x_direction * pong.ball.speed;

    if (pong.ball.x <= MIN_BALL_X) {
        pong.ball.x           = MIN_BALL_X;
        pong.ball.x_direction = 1;
    }

    if (pong.ball.x >= MAX_BALL_X) {
        pong.ball.x           = MAX_BALL_X;
        pong.ball.x_direction = -1;
    }

    pong.ball.y += pong.ball.y_direction * pong.ball.speed;

    if (pong.ball.y <= MIN_BALL_Y) {
        pong.ball.y           = MIN_BALL_Y;
        pong.ball.y_direction = 1;
    }

    if (pong.ball.y >= MAX_BALL_Y) {
        pong.ball.y           = MAX_BALL_Y;
        pong.ball.y_direction = -1;
    }
}

void check_collision() {
    if ((pong.player.x <= (pong.ball.x + PONG_BALL_WIDTH)) &&
        ((pong.player.x + PONG_BAR_WIDTH) >= pong.ball.x) &&
        (pong.player.y <= (pong.ball.y + PONG_BALL_HEIGHT)) &&
        ((pong.player.y + PONG_BAR_HEIGHT) >= pong.ball.y)) {
        pong.ball.y_direction = pong.ball.y > pong.player.y ? 1 : -1;
        pong.ball.x_direction = pong.ball.x > pong.player.x ? 1 : -1;
        pong.score++;
    }

    if (pong.ball.y >= MAX_BALL_Y) {
        pong.score--;
    }
}

void check_score() {
    if (pong.score == 0) {
        pong.state = STATE_LOST;
    } else if (pong.score == 100) {
        pong.state = STATE_WON;
    } else if (pong.score > pong.ball.next_speed_increase_at && pong.ball.speed < MAX_BALL_SPEED) {
        pong.ball.speed++;
        pong.ball.next_speed_increase_at += BALL_SPEED_INCREASE_POINTS;
    }
}

void update_screen() {
    gpu_clear();
    gpu_blit(pong.ball.x, pong.ball.y, PONG_BALL_WIDTH, PONG_BALL_HEIGHT, img_pong_ball);
    gpu_blit(pong.player.x, pong.player.y, PONG_BAR_WIDTH, PONG_BAR_HEIGHT, img_pong_bar);

    gpu_print_small(GPU_PRINT_RIGHT - 5, 5, "SCORE: %03d", pong.score);

    if (pong.state == STATE_WON) {
        gpu_print_small(5, 5, "YOU WON!");
    } else if (pong.state == STATE_LOST) {
        gpu_print_small(5, 5, "YOU LOST!");
    }

    gpu_sync();
}

void state_won() {
}

void state_lost() {
}

void game_pong_loop() {
    if (pong.state == STATE_IN_GAME) {
        update_player();
        update_ball();
        check_collision();
        check_score();
    }

    update_screen();
}