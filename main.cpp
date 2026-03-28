#include "CroixPharma.h"
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <random>
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <string>

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_LEFT  1002
#define KEY_RIGHT 1003

enum Direction { DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };
enum Panel { PANEL_LEFT = 0, PANEL_RIGHT = 1, PANEL_TOP = 2, PANEL_CENTER = 3, PANEL_BOTTOM = 4 };

static Panel direction_to_panel(uint8_t dir) {
    switch (dir) {
        case DIR_UP:    return PANEL_TOP;
        case DIR_DOWN:  return PANEL_BOTTOM;
        case DIR_LEFT:  return PANEL_LEFT;
        case DIR_RIGHT: return PANEL_RIGHT;
        default:        return PANEL_CENTER;
    }
}

uint8_t game[10] = {0};
uint8_t bitmap[SIZE][SIZE] = {{0}};
int current_round = 0;

bool player_turn = false;
bool loose = false;
bool win   = false;

CroixPharma croix;

static const char *get_sound_player() {
#ifdef __APPLE__
    return "afplay";
#else
    return "aplay";
#endif
}

static void play_sound_async(const char *file_path) {
    std::string cmd = std::string(get_sound_player()) + " \"" + file_path + "\" >/dev/null 2>&1 &";
    system(cmd.c_str());
}

void set_pixel(int x, int y, uint8_t val) {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) {
        return;
    }

    bool in_top = (x >= 8 && x < 16 && y >= 0 && y < 8);
    bool in_mid = (x >= 0 && x < 24 && y >= 8 && y < 16);
    bool in_bot = (x >= 8 && x < 16 && y >= 16 && y < 24);

    if (!in_top && !in_mid && !in_bot) {
        return;
    }

    bitmap[y][x] = val ? HIGH : LOW;
}

void clear_frame() {
    memset(bitmap, 0, sizeof(bitmap));
}

void fill_frame() {
    memset(bitmap, HIGH, sizeof(bitmap));
}

void send_frame() {
    croix.writeBitmap(bitmap);
}

void setup_input() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void flush_input() {
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {}
}

int get_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;

    if (c == '\x1b') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return -1;
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return -1;
        }
        if (seq[0] == '[') {
            if (seq[1] == 'A') return KEY_UP;
            if (seq[1] == 'B') return KEY_DOWN;
            if (seq[1] == 'C') return KEY_RIGHT;
            if (seq[1] == 'D') return KEY_LEFT;
        }
    }

    return (unsigned char)c;
}

bool check_correct(int round, int key_code) {
    bool ok = false;
    if (key_code == KEY_UP    && game[round] == DIR_UP)    ok = true;
    if (key_code == KEY_DOWN  && game[round] == DIR_DOWN)  ok = true;
    if (key_code == KEY_LEFT  && game[round] == DIR_LEFT)  ok = true;
    if (key_code == KEY_RIGHT && game[round] == DIR_RIGHT) ok = true;
    return ok;
}

void show_panel(Panel panel) {
    clear_frame();
    const char *sound_file = NULL;

    if (panel == PANEL_LEFT) {
        sound_file = "left.wav";
        for (int y = 8; y < 16; y++)
            for (int x = 0; x < 8; x++)
                set_pixel(x, y, HIGH);
    } else if (panel == PANEL_RIGHT) {
        sound_file = "right.wav";
        for (int y = 8; y < 16; y++)
            for (int x = 16; x < 24; x++)
                set_pixel(x, y, HIGH);
    } else if (panel == PANEL_TOP) {
        sound_file = "top.wav";
        for (int y = 0; y < 8; y++)
            for (int x = 8; x < 16; x++)
                set_pixel(x, y, HIGH);
    } else if (panel == PANEL_CENTER) {
        for (int y = 16; y < 24; y++)
            for (int x = 8; x < 16; x++)
                set_pixel(x, y, HIGH);
    } else if (panel == PANEL_BOTTOM) {
        sound_file = "bottom.wav";
        for (int y = 8; y < 16; y++)
            for (int x = 8; x < 16; x++)
                set_pixel(x, y, HIGH);
    }

    if (sound_file != NULL) {
        play_sound_async(sound_file);
    }

    send_frame();
}

void show_round(int round) {
    for (int i = 0; i <= round; i++) {
        Panel panel = direction_to_panel(game[i]);
        show_panel(panel);
        usleep(500000);
        clear_frame();
        send_frame();
        usleep(200000);
    }
}

void show_current_round(int round) {
    Panel panel = direction_to_panel(game[round]);
    show_panel(panel);
    usleep(500000);
    clear_frame();
    send_frame();
    usleep(200000);
}

void game_loose() {
    play_sound_async("loose.wav");
    for (int i = 0; i < 5; i++) {
        fill_frame();
        send_frame();
        usleep(200000);
        clear_frame();
        send_frame();
        usleep(200000);
    }
}

void game_win() {
    fill_frame();
    send_frame();
}

int main(void) {
    std::mt19937 rng(static_cast<unsigned>(time(NULL)));
    std::uniform_int_distribution<int> dist(0, 3);

    for (int i = 0; i < 10; i++) {
        game[i] = dist(rng);
    }

    if (wiringPiSetupGpio() < 0) {
        return 1;
    }

    croix.begin();
    croix.setSide(CroixPharma::BOTH);

    setup_input();
    clear_frame();
    send_frame();

    while (true) {
        if (!player_turn && !loose && !win) {
            show_round(current_round);
            flush_input();
            player_turn = true;
        }
        else if (player_turn && !loose && !win) {
            for (int i = 0; i <= current_round; i++) {
                bool correct = false;
                while (!correct) {
                    int key = get_key();
                    if (key == KEY_UP || key == KEY_DOWN ||
                        key == KEY_LEFT || key == KEY_RIGHT) {
                        if (check_correct(i, key)) {
                            show_current_round(i);
                            correct = true;
                        } else {
                            loose   = true;
                            correct = true;
                        }
                    }
                    // touche non-directionnelle ignorée silencieusement
                }
                if (loose) break;
            }

            if (!loose) {
                player_turn = false;
                current_round++;
                if (current_round >= 10) {
                    win = true;
                }
            }
        }
        else if (loose) {
            game_loose();
            break;
        }
        else if (win) {
            game_win();
            break;
        }
    }
    return 0;
}