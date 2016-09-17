#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define TIMEOUT_USEC 2000000

#ifdef __unix__
#include <sys/time.h>
#include <unistd.h>

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
#endif

#include "tables.h"

static int
hex_to_bit(int q, int r)
{
    if (q < -4 || q > 4 || r < -4 || r > 4)
        return -1;
    else
        return store_map[q + 4][r + 4];
}

static uint64_t
rotl(const uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t
xoroshiro128plus(uint64_t s[static 2])
{
    const uint64_t s0 = s[0];
    uint64_t s1 = s[1];
    const uint64_t result = s0 + s1;
    s1 ^= s0;
    s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
    s[1] = rotl(s1, 36); // c
    return result;
}

static uint64_t
splitmix64(uint64_t *x)
{
    uint64_t z = (*x += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
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

#define MCTS_NULL ((uint32_t)-1)
struct mcts {
    uint64_t rng[2];              // random number state
    uint64_t root_state[2];       // board state at root node
    uint32_t root;                // root node index
    uint32_t free;                // index of head of free list
    uint32_t node_avail;          // total nodes available
    uint32_t node_count;          // total nodes ever touched
    int root_turn;                // whose turn it is at root node
    struct mcts_node {
        uint32_t wins[61];        // win counter for each move
        uint32_t playouts[61];    // number of playouts for this play
        uint32_t next[61];        // next node when taking this play
        int8_t   avail_plays[61]; // list of valid plays for this node
        uint8_t  nplays;          // number of plays for this node
    } nodes[];
};

static uint32_t
mcts_alloc(struct mcts *m, uint64_t taken)
{
    uint32_t nodei;
    if (m->free != MCTS_NULL) {
        nodei = m->free;
        m->free = m->nodes[m->free].next[0];
    } else if (m->node_count < m->node_avail) {
        nodei = m->node_count++;
    } else {
        fprintf(stderr, "warning: out of memory\n");
        return MCTS_NULL;
    }
    struct mcts_node *n = m->nodes + nodei;
    n->nplays = 0;
    for (int i = 0; i < 61; i++) {
        if (!((taken >> i) & 1)) {
            n->avail_plays[n->nplays++] = i;
            n->wins[i] = 0;
            n->playouts[i] = 0;
        }
        n->next[i] = MCTS_NULL;
    }
    return nodei;
}

static void
mcts_free(struct mcts *m, uint32_t node)
{
    if (node != MCTS_NULL) {
        struct mcts_node *n = m->nodes + node;
        for (int i = 0; i < 61; i++)
            mcts_free(m, n->next[i]);
        n->next[0] = m->free;
        m->free = node;
    }
}

static struct mcts *
mcts_init(void *buf, size_t bufsize, uint64_t state[2], int turn)
{
    struct mcts *m = buf;
    m->free = MCTS_NULL;
    m->node_avail = (bufsize  - sizeof(*m)) / sizeof(m->nodes[0]);
    m->node_count = 0;
    m->root = mcts_alloc(m, state[0] | state[1]);
    m->root_state[0] = state[0];
    m->root_state[1] = state[1];
    m->root_turn = turn;
    uint64_t seed = 0; //uepoch(); FIXME
    m->rng[0] = splitmix64(&seed);
    m->rng[1] = splitmix64(&seed);
    return m->root == MCTS_NULL ? NULL : m;
}

static int
mcts_is_valid(struct mcts_node *n, int tile)
{
    for (int i = 0; i < n->nplays; i++)
        if (n->avail_plays[i] == tile)
            return 1;
    return 0;
}

static void
mcts_advance(struct mcts *m, int tile)
{
    uint32_t old_root = m->root;
    struct mcts_node *root = m->nodes + old_root;
    if (!mcts_is_valid(root, tile))
        fprintf(stderr, "error: invalid move, %d\n", tile);
    assert(mcts_is_valid(root, tile));
    m->root_state[m->root_turn] |= UINT64_C(1) << tile;
    m->root_turn = !m->root_turn;
    m->root = root->next[tile];
    root->next[tile] = MCTS_NULL; // prevents free
    mcts_free(m, old_root);
    if (m->root == MCTS_NULL) {
        /* never explored this branch, allocate it */
        uint64_t taken = m->root_state[0] | m->root_state[1];
        m->root = mcts_alloc(m, taken);
        assert(m->root != MCTS_NULL);
    }
}

static int
mcts_playout(struct mcts *m, uint32_t node, const uint64_t s[2], int turn)
{
    uint64_t copy[2] = {s[0], s[1]};
    struct mcts_node *n = m->nodes + node;
    int play = n->avail_plays[xoroshiro128plus(m->rng) % n->nplays];
    assert(play >= 0 && play <= 61);
    uint64_t play_bit = UINT64_C(1) << play;
    copy[turn] |= play_bit;
    uint64_t dummy;
    switch (check(copy[turn], play, &dummy)) {
        case CHECK_RESULT_WIN:
            n->playouts[play]++;
            n->wins[play]++;
            return turn;
        case CHECK_RESULT_LOSS:
            n->playouts[play]++;
            return !turn;
        case CHECK_RESULT_NOTHING:
            break;
    }
    if (n->next[play] == MCTS_NULL) {
        n->next[play] = mcts_alloc(m, copy[0] | copy[1]);
        if (n->next[play] == MCTS_NULL)
            return -1; // out of memory
    }
    int winner = mcts_playout(m, n->next[play], copy, !turn);
    if (winner != -1)
        n->playouts[play]++;
    if (winner == turn)
        n->wins[play]++;
    return winner;
}

static int
mcts_choose(struct mcts *m, uint64_t timeout_usec)
{
    uint64_t stop = os_uepoch() + timeout_usec;
    int r;
    do {
        for (int i = 0; i < 1024; i++) {
            r = mcts_playout(m, m->root, m->root_state, m->root_turn);
            if (r < 0)
                break;
        }
    } while (r >= 0 && os_uepoch() < stop);
    if (r < 0)
        fprintf(stderr, "note: early bailout, out of memory\n");

    int best = -1;
    float best_ratio = -1.0f;
    struct mcts_node *n = m->nodes + m->root;
    for (int i = 0; i < 61; i++) {
        if (n->playouts[i]) {
            float ratio = n->wins[i] / (float)n->playouts[i];
            if (ratio > best_ratio) {
                best = i;
                best_ratio = ratio;
            }
        }
    }
    assert(best != -1);
    return best;
}

struct mcts_stats {
    uint32_t free;
    uint32_t used;
};

static void
mcts_stats(struct mcts *m, struct mcts_stats *s)
{
    s->free = 0;
    for (uint32_t p = m->free; p != MCTS_NULL; p = m->nodes[p].next[0])
        s->free++;
    s->used = m->node_count - s->free;
}

int
main(void)
{
    uint64_t board[2] = {0, 0};
    unsigned turn = 0;

    size_t physical_memory = os_physical_memory();
    size_t size = physical_memory;
    void *buf;
    do {
        size *= 0.8f;
        buf = malloc(size);
    } while (!buf);
    printf("%zu MB physical memory found, AI will use %zu MB\n",
           physical_memory / 1024 / 1024, size / 1024 / 1024);
    struct mcts *mcts = mcts_init(buf, size, board, turn);

    for (;;) {
        display(board[0], board[1], 0);
        char line[64];
        int bit;
        if (turn == 0) {
            for (;;) {
                fputs("\n> ", stdout);
                fflush(stdout);
                fgets(line, sizeof(line), stdin);
                int q, r;
                if (notation_to_hex(line, &q, &r)) {
                    bit = hex_to_bit(q, r);
                    if (bit == -1) {
                        printf("Invalid move (out of bounds)\n");
                    } else if (((board[0] >> bit) & 1) ||
                               ((board[1] >> bit) & 1)) {
                        printf("Invalid move (tile not free)\n");
                    } else {
                        break;
                    }
                }
            }
        } else {
            puts("AI is thinking ...");
            fflush(stdout);
            bit = mcts_choose(mcts, TIMEOUT_USEC);
            struct mcts_stats stats;
            mcts_stats(mcts, &stats);
            printf("free  = %" PRIu32 "\n", stats.free);
            printf("used  = %" PRIu32 "\n", stats.used);
            printf("avail = %" PRIu32 "\n", mcts->node_avail);
            printf("root  = %" PRIu32 "\n", mcts->root);
        }
        mcts_advance(mcts, bit);
        uint64_t where;
        board[turn] |= UINT64_C(1) << bit;
        switch (check(board[turn], bit, &where)) {
            case CHECK_RESULT_NOTHING: {
                turn = !turn;
            } break;
            case CHECK_RESULT_LOSS: {
                display(board[0], board[1], where);
                printf("player %c loses!\n", "ox"[turn]);
                exit(0);
            } break;
            case CHECK_RESULT_WIN: {
                display(board[0], board[1], where);
                printf("player %c wins!\n", "ox"[turn]);
                exit(0);
            } break;
        }
    }

    free(mcts);
    return 0;
}
