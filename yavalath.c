#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "tables.h"

static int
hex_to_bit(int q, int r)
{
    if (q < -4 || q > 4 || r < -4 || r > 4)
        return -1;
    else
        return store_map[q + 4][r + 4];
}

static void
display(uint64_t w, uint64_t b, uint64_t highlight)
{
    for (int q = -4; q <= 4; q++) {
        printf("%c ", 'a' + q + 4);
        for (int s = 0; s < q + 4; s++)
            putchar(' ');
        for (int r = -4; r <= 4; r++) {
            int bit = hex_to_bit(q, r);
            if (bit == -1)
                fputs("  ", stdout);
            else {
                int h = highlight >> bit & 1;
                if (h)
                    fputs("\x1b[91;1m", stdout);
                if ((w >> bit) & 1)
                    putchar('o');
                else if ((b >> bit) & 1)
                    putchar('x');
                else
                    putchar('.');
                if (h)
                    fputs("\x1b[0m", stdout);
                putchar(' ');
            }
        }
        putchar('\n');
    }
}

static int
notation_to_hex(const char *s, int *q, int *r)
{
    if (s[0] < 'a' || s[1] > 'i')
        return 0;
    if (s[1] < '1' || s[1] > '9')
        return 0;
    *q = s[0] - 'a' - 4;
    *r = s[1] - "123455555"[*q + 4];
    return 1;
}

enum check_result {
    CHECK_RESULT_LOSS = -1,
    CHECK_RESULT_NOTHING = 0,
    CHECK_RESULT_WIN = 1,
};

static enum check_result
check(uint64_t c, int p, uint64_t *where)
{
    for (int i = 0; i < 12; i++) {
        uint64_t mask = pattern_win[p][i];
        if (mask && (c & mask) == mask) {
            *where = mask;
            return CHECK_RESULT_WIN;
        }
    }
    for (int i = 0; i < 9; i++) {
        uint64_t mask = pattern_lose[p][i];
        if (mask && (c & mask) == mask) {
            *where = mask;
            return CHECK_RESULT_LOSS;
        }
    }
    *where = 0;
    return CHECK_RESULT_NOTHING;
}

int
main(void)
{
    uint64_t board[2] = {0, 0};
    unsigned t = 0;
    for (;;) {
        display(board[0], board[1], 0);
        int q, r;
        char line[64];
        do {
            fputs("\n> ", stdout);
            fflush(stdout);
            fgets(line, sizeof(line), stdin);
        } while (!notation_to_hex(line, &q, &r));
        int bit = hex_to_bit(q, r);
        if (bit == -1) {
            printf("Invalid move (out of bounds)\n");
        } else if ((board[0] >> bit & 1) || (board[1] >> bit & 1)) {
            printf("Invalid move (tile not free)\n");
        } else {
            uint64_t where;
            board[t & 1] |= UINT64_C(1) << bit;
            switch (check(board[t & 1], bit, &where)) {
                case CHECK_RESULT_NOTHING: {
                    t++;
                } break;
                case CHECK_RESULT_LOSS: {
                display(board[0], board[1], where);
                printf("player %c loses!\n", "ox"[t & 1]);
                exit(0);
                } break;
                case CHECK_RESULT_WIN: {
                    display(board[0], board[1], where);
                    printf("player %c wins!\n", "ox"[t & 1]);
                exit(0);
                } break;
            }
        }
    }

    return 0;
}
