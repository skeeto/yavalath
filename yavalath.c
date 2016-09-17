#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
hex_norm(int q, int r)
{
    return (abs(q) + abs(q + r) + abs(r)) / 2;
}

static int8_t store_map[9][9];
static struct {int8_t q, r;} load_map[61];
static uint64_t pattern3[61][9];
static uint64_t pattern4[61][12];

static void
display(uint64_t w, uint64_t b)
{
    for (int q = -4; q <= 4; q++) {
        printf("%c ", 'a' + q + 4);
        for (int s = 0; s < q + 4; s++)
            putchar(' ');
        for (int r = -4; r <= 4; r++) {
            int bit = store_map[q + 4][r + 4];
            if (bit == -1)
                fputs("  ", stdout);
            else {
                if ((w >> bit) & 1)
                    putchar('o');
                else if ((b >> bit) & 1)
                    putchar('x');
                else
                    putchar('.');
                putchar(' ');
            }
        }
        putchar('\n');
    }
}

static int
check(uint64_t c, int p)
{
    for (int i = 0; i < 12; i++) {
        uint64_t mask = pattern4[p][i];
        if (mask && (c & mask) == mask)
            return 1;
    }
    for (int i = 0; i < 9; i++) {
        uint64_t mask = pattern3[p][i];
        if (mask && (c & mask) == mask)
            return -1;
    }
    return 0;
}

int
main(void)
{
    memset(store_map, -1, sizeof(store_map));
    int count = 0;
    for (int q = -4; q <= 4; q++) {
        for (int r = -4; r <= 4; r++) {
            if (hex_norm(q, r) < 5) {
                load_map[count].q = q;
                load_map[count].r = r;
                store_map[q + 4][r + 4] = count++;
            }
        }
    }

    for (int q = -4; q <= 4; q++) {
        for (int r = -4; r <= 4; r++) {
            int center_bit = store_map[q + 4][r + 4];
            if (center_bit == -1)
                continue;
            char hex_axes[] = {1, 0, 0, 1, -1, 1};
            for (int length = 3; length <= 4; length++) {
                int maski = 0;
                for (int d = 0; d < 3; d++) {
                    int dq = hex_axes[d * 2 + 0];
                    int dr = hex_axes[d * 2 + 1];
                    for (int offset = 1 - length; offset <= 0; offset++) {
                        uint64_t mask = 0;
                        int bits_set = 0;
                        for (int i = 0; i < length; i++) {
                            int tq = q + dq * (offset + i);
                            int tr = r + dr * (offset + i);
                            if (tq >= -4 && tq <= 4 && tr >= -4 && tr <= 4) {
                                int bit = store_map[tq + 4][tr + 4];
                                if (bit != -1) {
                                    mask |= UINT64_C(1) << bit;
                                    bits_set++;
                                }
                            }
                        }
                        if (bits_set == length) {
                            if (length == 3)
                                pattern3[center_bit][maski++] = mask;
                            else
                                pattern4[center_bit][maski++] = mask;
                        }
                    }
                }
            }
        }
    }

    uint64_t board[2] = {0, 0};
    for (unsigned t = 0; ; t++) {
        display(board[0], board[1]);
        int q, r;
        char line[64];
        do {
            fputs("> ", stdout);
            fflush(stdout);
            fgets(line, sizeof(line), stdin);
        } while (sscanf(line, " %d %d", &q, &r) != 2);
        int bit = store_map[q + 4][r + 4];
        board[t & 1] |= UINT64_C(1) << bit;
        int c = check(board[t & 1], bit);
        if (c == -1) {
            display(board[0], board[1]);
            printf("player %c loses!\n", "ox"[t & 1]);
            break;
        } else if (c == 1) {
            display(board[0], board[1]);
            printf("player %c wins!\n", "ox"[t & 1]);
            break;
        }
    }

    return 0;
}
