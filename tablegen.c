#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static uint64_t pattern_lose[61][9];
static uint64_t pattern_win[61][12];
static int8_t store_map[9][9];

static int
hex_norm(int q, int r)
{
    return (abs(q) + abs(q + r) + abs(r)) / 2;
}

int
main(void)
{
    /* Map hex tiles to bit storage. */
    memset(store_map, -1, sizeof(store_map));
    int count = 0;
    for (int q = -4; q <= 4; q++)
        for (int r = -4; r <= 4; r++)
            if (hex_norm(q, r) < 5)
                store_map[q + 4][r + 4] = count++;

    /* Compute bitmasks defining the rules. */
    for (int q = -4; q <= 4; q++) {
        for (int r = -4; r <= 4; r++) {
            int center_bit = store_map[q + 4][r + 4];
            if (center_bit == -1)
                continue;
            int hex_axes[] = {1, 0, 0, 1, -1, 1};
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
                                pattern_lose[center_bit][maski++] = mask;
                            else
                                pattern_win[center_bit][maski++] = mask;
                        }
                    }
                }
            }
        }
    }

    /* Write out bitmask tables. */
    printf("#include <stdint.h>\n\n");
    printf("static const int8_t store_map[9][9] = {\n");
    for (unsigned i = 0; i < 9; i++) {
        printf("    {");
        for (unsigned j = 0; j < 9; j++)
            printf("%2d%s", store_map[i][j], j == 8 ? "" : ", ");
        printf("},\n");
    }
    printf("};\n\n");
    printf("static const uint64_t pattern_lose[61][9] = {\n");
    for (unsigned i = 0; i < 61; i++) {
        printf("    {\n");
        for (unsigned j = 0; j < 9; j++)
            printf("%s0x%016" PRIx64 "%s",
                   j % 3 == 0 ? "        " : ", ",
                   pattern_lose[i][j],
                   j % 3 == 2 ? ",\n" : "");
        printf("    },\n");
    }
    printf("};\n\n");
    printf("static const uint64_t pattern_win[61][12] = {\n");
    for (unsigned i = 0; i < 61; i++) {
        printf("    {\n");
        for (unsigned j = 0; j < 12; j++)
            printf("%s0x%016" PRIx64 "%s",
                   j % 3 == 0 ? "        " : ", ",
                   pattern_win[i][j],
                   j % 3 == 2 ? ",\n" : "");
        printf("    },\n");
    }
    printf("};\n");
    return 0;
}
