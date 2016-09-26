#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "yavalath.h"

#define TIMEOUT_MSEC (15 * 1000UL)
#define MAX_PLAYOUTS UINT32_C(-1)
#define MEMORY_USAGE 0.8f

#ifdef __unix__
#include <unistd.h>
#include <sys/time.h>

static uint64_t
os_uepoch(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return UINT64_C(1000000) * tv.tv_sec + tv.tv_usec;
}

static size_t
os_physical_memory(void)
{
    size_t pages = sysconf(_SC_PHYS_PAGES);
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    return pages * page_size;
}

static void
os_color(int color)
{
    if (color)
        printf("\x1b[%d;1m", 90 + color);
    else
        fputs("\x1b[0m", stdout);
}

static void
os_restart_line(void)
{
    puts("\x1b[F");
}

static void
os_finish(void)
{
    // nothing
}

#elif _WIN32
#include <windows.h>

static uint64_t
os_uepoch(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t tt = ft.dwHighDateTime;
    tt <<= 32;
    tt |= ft.dwLowDateTime;
    tt /=10;
    tt -= UINT64_C(11644473600000000);
    return tt;
}

static size_t
os_physical_memory(void)
{
    MEMORYSTATUSEX status = {.dwLength = sizeof(status)};
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys;
}

static void
os_color(int color)
{
    WORD bits = color ? FOREGROUND_INTENSITY : 0;
    if (!color || color & 0x1)
        bits |= FOREGROUND_RED;
    if (!color || color & 0x2)
        bits |= FOREGROUND_GREEN;
    if (!color || color & 0x4)
        bits |= FOREGROUND_BLUE;
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), bits);
}

static void
os_restart_line(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(out, &info);
    info.dwCursorPosition.X = 0;
    SetConsoleCursorPosition(out, info.dwCursorPosition);
}

static void
os_finish(void)
{
    getchar(); // leave window open
}
#endif

static void
display(uint64_t w, uint64_t b, uint64_t highlight, int color)
{
    for (int q = -4; q <= 4; q++) {
        printf("%c ", 'a' + q + 4);
        for (int s = 0; s < q + 4; s++)
            putchar(' ');
        for (int r = -4; r <= 4; r++) {
            int bit = yavalath_hex_to_bit(q, r);
            if (bit == -1)
                fputs("  ", stdout);
            else {
                int h = highlight >> bit & 1;
                if (h)
                    os_color(color);
                if ((w >> bit) & 1)
                    putchar('o');
                else if ((b >> bit) & 1)
                    putchar('x');
                else
                    putchar('.');
                if (h)
                    os_color(0);
                putchar(' ');
            }
        }
        putchar('\n');
    }
}

struct playout_limits {
    unsigned long msecs;
    uint32_t playouts;
};

static void
playout_to_limit(void *buf, struct playout_limits *limits)
{
    uint64_t timeout = os_uepoch() + limits->msecs * 1000;
    uint32_t playouts = 0;
    uint32_t iterations = 64 * 1024;
    enum yavalath_result r;
    do {
        if (playouts + iterations > limits->playouts)
            iterations = limits->playouts - playouts;
        uint64_t time_start = os_uepoch();
        r = yavalath_ai_playout(buf, iterations);
        uint64_t time_end = os_uepoch();
        uint64_t run_time = time_end - time_start;
        if (r == YAVALATH_SUCCESS)
            playouts += iterations;
        if (run_time > 300000)
            iterations *= 0.85f;
        else if (run_time < 250000)
            iterations *= 1.18f;
        os_restart_line();
        uint32_t nodes_used = yavalath_ai_nodes_used(buf);
        uint32_t nodes_total = yavalath_ai_nodes_total(buf);
        printf("%.2f%% memory usage, %" PRIu32 " playouts, %0.1fs remaining",
               100 * nodes_used / (double)nodes_total,
               yavalath_ai_total_playouts(buf),
               timeout / 1e6 - time_end / 1e6);
        fflush(stdout);
    } while (r == YAVALATH_SUCCESS &&
             os_uepoch() < timeout &&
             playouts < limits->playouts);
    puts(" ... done\n");
}

static void
print_usage(void)
{
    printf("yavalath-cli [options]\n");
    printf("  -0<h|c>       Select human or computer for player 0\n");
    printf("  -1<h|c>       Select human or computer for player 1\n");
    printf("  -t<secs>      Set AI timeout in (fractional) seconds\n");
    printf("  -p<playouts>  Set maximum number of playouts for AI\n");
    printf("  -m<0.0-1.0>   Fraction of physical memory to use for AI\n");
    printf("  -h            Print this help text\n\n");

    printf("For example, to see AI vs. AI with 1 minute turns:\n");
    printf("  $ yavalath-cli -0c -1c -t60\n");
}

int
main(int argc, char **argv)
{
    uint64_t seed = os_uepoch();
    uint64_t board[2] = {0, 0};
    unsigned turn = 0;
    float memory_usage = MEMORY_USAGE;
    enum player_type {
        PLAYER_HUMAN,
        PLAYER_AI
    } player_type[2] = {
        PLAYER_HUMAN, PLAYER_AI
    };
    struct playout_limits limits = {
        .msecs = TIMEOUT_MSEC,
        .playouts = MAX_PLAYOUTS,
    };

    /* Mini getopt() */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            char *p = argv[i] + 1;
            switch (*p) {
                case '0':
                case '1':
                    if (!p[1])
                        goto missing;
                    switch (p[1]) {
                        case 'h':
                            player_type[*p - '0'] = PLAYER_HUMAN;
                            break;
                        case 'c':
                            player_type[*p - '0'] = PLAYER_AI;
                            break;
                        default:
                            goto fail;
                    }
                    break;
                case 't':
                    if (!p[1])
                        goto missing;
                    limits.msecs = strtod(p + 1, 0) * 1000;
                    break;
                case 'p':
                    if (!p[1])
                        goto missing;
                    limits.playouts = strtoll(p + 1, 0, 10);
                    break;
                case 'm':
                    if (!p[1])
                        goto missing;
                    memory_usage = strtof(p + 1, 0);
                    break;
                case 'h':
                    print_usage();
                    exit(0);
            }
        } else {
            goto fail;
        }
        continue;
  missing:
        fprintf(stderr, "yavalath-cli: missing argument, %s\n", argv[i]);
        exit(-1);
  fail:
        fprintf(stderr, "yavalath-cli: bad argument, %s\n", argv[i]);
        exit(-1);
    }

    size_t physical_memory = os_physical_memory();
    size_t size = physical_memory * memory_usage;
    void *buf;
    do {
        size *= 0.8;
        buf = malloc(size);
    } while (!buf);
    yavalath_ai_init(buf, size, 0, 0, seed);
    printf("%zu MB physical memory found, "
           "AI will use %zu MB (%" PRIu32 " nodes)\n",
           physical_memory / 1024 / 1024,
           size / 1024 / 1024,
           yavalath_ai_nodes_total(buf));

    uint64_t last_play = 0;
    for (;;) {
        display(board[0], board[1], last_play, 3);
        fflush(stdout);
        char line[64];
        int bit = -1;
        switch (player_type[turn]) {
            case PLAYER_HUMAN:
                for (;;) {
                    fputs("\n> ", stdout);
                    fflush(stdout);
                    if (!fgets(line, sizeof(line), stdin))
                        return -1; // EOF
                    bit = yavalath_notation_to_bit(line);
                    if (bit == -1) {
                        printf("Invalid move (out of bounds)\n");
                    } else if (((board[0] >> bit) & 1) ||
                               ((board[1] >> bit) & 1)) {
                        printf("Invalid move (tile not free)\n");
                    } else {
                        break;
                    }
                }
                break;
            case PLAYER_AI:
                putchar('\n');
                playout_to_limit(buf, &limits);
                bit = yavalath_ai_best_move(buf);
                break;
        }
        last_play = UINT64_C(1) << bit;
        yavalath_ai_advance(buf, bit);
        board[turn] |= UINT64_C(1) << bit;
        uint64_t where;
        enum yavalath_game_result result;
        result = yavalath_check(board[turn], board[!turn], bit, &where);
        switch (result) {
            case YAVALATH_GAME_UNRESOLVED: {
                turn = !turn;
            } break;
            case YAVALATH_GAME_LOSS: {
                display(board[0], board[1], where, 1);
                printf("player %c loses!\n", "ox"[turn]);
                goto done;
            } break;
            case YAVALATH_GAME_WIN: {
                display(board[0], board[1], where, 4);
                printf("player %c wins!\n", "ox"[turn]);
                goto done;
            } break;
            case YAVALATH_GAME_DRAW: {
                display(board[0], board[1], where, 5);
                printf("draw game!\n");
                goto done;
            } break;
        }
    }

done:
    free(buf);
    os_finish();
    return 0;
}
