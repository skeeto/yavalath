#include <math.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "yavalath.h"
#include "tables.h"

#ifndef YAVALATH_C
#  define YAVALATH_C  0.5f
#endif

#define REWARD_WIN   1.0f
#define REWARD_DRAW -0.1f
#define REWARD_LOSS -1.0f

#define DRAW  100

static int
hex_to_bit(int q, int r)
{
    if (q < -4 || q > 4 || r < -4 || r > 4)
        return -1;
    else
        return store_map[q + 4][r + 4];
}
static int
bit_to_hex(int bit, int *q, int *r)
{
    // TODO: closed form
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 9; j++)
            if (store_map[i][j] == bit) {
                *q = i - 4;
                *r = j - 4;
                return 1;
            }
    return 0;
}

static uint64_t
rotl(const uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t
xoroshiro128plus(uint64_t *s)
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

static int
hex_to_notation(char *s, int q, int r)
{
    if (q < -4 || q > 4 || r < -4 || r > 4)
        return 0;
    s[0] = q + 'a' + 4;
    s[1] = r + "123455555"[q + 4];
    s[2] = 0;
    return 1;
}

static enum yavalath_game_result
check(uint64_t who, uint64_t opponent, int where, uint64_t *how)
{
    for (int i = 0; i < 12; i++) {
        uint64_t mask = pattern_win[where][i];
        if (mask && (who & mask) == mask) {
            *how = mask;
            return YAVALATH_GAME_WIN;
        }
    }
    for (int i = 0; i < 9; i++) {
        uint64_t mask = pattern_lose[where][i];
        if (mask && (who & mask) == mask) {
            *how = mask;
            return YAVALATH_GAME_LOSS;
        }
    }
    *how = 0;
    if ((who | opponent) == UINT64_C(0x1fffffffffffffff))
        return YAVALATH_GAME_DRAW;
    return YAVALATH_GAME_UNRESOLVED;
}

static uint64_t
state_hash(uint64_t a, uint64_t b)
{
    uint64_t rng[2];
    rng[0] = splitmix64(&a);
    rng[1] = splitmix64(&b);
    return xoroshiro128plus(rng);
}

#define MCTS_NULL      ((uint32_t)-1)
#define MCTS_DRAW      ((uint32_t)-2)
#define MCTS_WIN0      ((uint32_t)-3)
#define MCTS_WIN1      ((uint32_t)-4)
struct mcts {
    uint64_t rng[2];              // random number state
    uint32_t root;                // root node index
    uint32_t free;                // index of head of free list
    uint32_t nodes_avail;         // total nodes available
    uint32_t nodes_allocated;     // total number allocated
    int root_turn;                // whose turn it is at root node
    struct mcts_node {
        uint32_t head;            // head of hash list for this slot
        uint32_t chain;           // next item in hash table list
        uint64_t state[2];        // the game state at this node
        uint32_t total_playouts;  // number of playouts through this node
        float    reward[61];      // win counter for each move
        uint32_t playouts[61];    // number of playouts for this play
        uint32_t next[61];        // next node when taking this play
        uint16_t refcount;        // number of nodes referencing this node
        uint8_t  unexplored;      // count of unexplored
    } nodes[];
};

static uint32_t
mcts_find(struct mcts *m, uint32_t list_head, const uint64_t state[2])
{
    while (list_head != MCTS_NULL) {
        struct mcts_node *n = m->nodes + list_head;
        if (n->state[0] == state[0] && n->state[1] == state[1])
            return list_head;
        list_head = n->chain;
    }
    return MCTS_NULL;
}

static uint32_t
mcts_alloc(struct mcts *m, const uint64_t state[2])
{
    uint64_t hash = state_hash(state[0], state[1]);
    uint32_t *head = &m->nodes[hash % m->nodes_avail].head;
    uint32_t nodei = mcts_find(m, *head, state);
    if (nodei != MCTS_NULL) {
        /* Node already exists, return it. */
        assert(m->nodes[nodei].refcount > 0);
        m->nodes[nodei].refcount++;
        return nodei;
    } if (m->free != MCTS_NULL) {
        /* Allocate a node. */
        nodei = m->free;
        m->free = m->nodes[m->free].chain;
        m->nodes_allocated++;
    } else {
        return MCTS_NULL;
    }

    /* Initiaize the node. */
    struct mcts_node *n = m->nodes + nodei;
    n->state[0] = state[0];
    n->state[1] = state[1];
    n->refcount = 1;
    n->total_playouts = 0;
    n->unexplored = 0;
    n->chain = *head;
    *head = nodei;
    uint64_t taken = state[0] | state[1];
    for (int i = 0; i < 61; i++) {
        n->reward[i] = 0.0f;
        n->playouts[i] = 0;
        n->next[i] = MCTS_NULL;
        if (!((taken >> i) & 1))
            n->unexplored++;
    }
    return nodei;
}

static void
mcts_free(struct mcts *m, uint32_t node)
{
    if (node < MCTS_WIN1) {
        struct mcts_node *n = m->nodes + node;
        assert(n->refcount);
        if (--n->refcount == 0) {
            m->nodes_allocated--;
            for (int i = 0; i < 61; i++)
                mcts_free(m, n->next[i]);
            uint64_t hash = state_hash(n->state[0], n->state[1]);
            uint32_t parent = m->nodes[hash % m->nodes_avail].head;
            if (parent == node) {
                m->nodes[hash % m->nodes_avail].head = n->chain;
            } else {
                while (m->nodes[parent].chain != node)
                    parent = m->nodes[parent].chain;
                m->nodes[parent].chain = n->chain;
            }
            n->chain = m->free;
            m->free = node;
        }
    }
}

static struct mcts *
mcts_init(void *buf,
          size_t bufsize,
          uint64_t state[2],
          int turn,
          uint64_t seed)
{
    struct mcts *m = buf;
    m->nodes_avail = (bufsize  - sizeof(*m)) / sizeof(m->nodes[0]);
    m->nodes_allocated = 0;
    m->rng[0] = splitmix64(&seed);
    m->rng[1] = splitmix64(&seed);
    m->free = 0;
    for (uint32_t i = 0; i < m->nodes_avail; i++) {
        m->nodes[i].head = MCTS_NULL;
        m->nodes[i].chain = i + 1;
    }
    m->nodes[m->nodes_avail - 1].chain = MCTS_NULL;
    m->root = mcts_alloc(m, state);
    m->root_turn = turn;
    return m->root == MCTS_NULL ? NULL : m;
}

static int
mcts_advance(struct mcts *m, int tile)
{
    uint32_t old_root = m->root;
    struct mcts_node *root = m->nodes + old_root;
    if (((root->state[0] | root->state[1]) >> tile) & 1)
        return 0;
    uint64_t state[2] = {root->state[0], root->state[1]};
    state[m->root_turn] |= UINT64_C(1) << tile;
    m->root_turn = !m->root_turn;
    m->root = root->next[tile];
    root->next[tile] = MCTS_NULL;  // prevents free
    mcts_free(m, old_root);
    if (m->root >= MCTS_WIN1) {
        /* never explored this branch, allocate it */
        m->root = mcts_alloc(m, state);
    }
    return 1;
}

static int
random_play_from_remaining(struct mcts_node *n, uint64_t *rng)
{
    uint64_t taken = n->state[0] | n->state[1];
    int options[61];
    int noptions = 0;
    for (int i = 0; i < 61; i++)
        if (!((taken >> i) & 1) && n->next[i] == MCTS_NULL)
            options[noptions++] = i;
    assert(noptions);
    return options[xoroshiro128plus(rng) % noptions];
}

static int
random_play_simple(uint64_t taken, uint64_t *rng)
{
    int options[61];
    int noptions = 0;
    for (int i = 0; i < 61; i++)
        if (!((taken >> i) & 1))
            options[noptions++] = i;
    assert(noptions);
    return options[xoroshiro128plus(rng) % noptions];
}

static int
mcts_playout_final(uint64_t *rng, uint64_t *state, int initial_turn)
{
    int turn = initial_turn;
    for (;;) {
        turn = !turn;
        uint64_t taken = state[0] | state[1];
        int play = random_play_simple(taken, rng);
        state[turn] |= UINT64_C(1) << play;
        uint64_t dummy;
        switch (check(state[turn], state[!turn], play, &dummy)) {
            case YAVALATH_GAME_WIN:
                return turn;
            case YAVALATH_GAME_LOSS:
                return !turn;
            case YAVALATH_GAME_DRAW:
                return DRAW;
            case YAVALATH_GAME_UNRESOLVED:
                break;
        }
    }
}

static int
mcts_playout(struct mcts *m, uint32_t node, int turn)
{
    if (node == MCTS_WIN0)
        return 0;
    else if (node == MCTS_WIN1)
        return 1;
    else if (node == MCTS_DRAW)
        return DRAW;
    assert(node != MCTS_NULL);

    struct mcts_node *n = m->nodes + node;
    if (n->total_playouts == UINT32_MAX)
        return -2; // more playouts would overflow
    int play = -1;
    if (!n->unexplored) {
        /* Use upper confidence bound (UCB1). */
        uint64_t taken = n->state[0] | n->state[1];
        float best_x = -INFINITY;
        float numerator = YAVALATH_C * logf(n->total_playouts);
        int best[61];
        int nbest = 0;
        for (int i = 0; i < 61; i++) {
            if (!((taken >> i) & 1)) {
                assert(n->playouts[i]);
                float mean = n->reward[i] / n->playouts[i];
                float x = mean + sqrtf(numerator / n->playouts[i]);
                if (x > best_x) {
                    best_x = x;
                    nbest = 1;
                    best[0] = i;
                } else if (x == best_x) {
                    best[nbest++] = i;
                }
            }
        }
        play = nbest == 1 ? best[0] : best[xoroshiro128plus(m->rng) % nbest];
        int winner = mcts_playout(m, n->next[play], !turn);
        if (winner >= 0) {
            n->playouts[play]++;
            n->total_playouts++;
        }
        if (winner == turn)
            n->reward[play] += REWARD_WIN;
        else if (winner == !turn)
            n->reward[play] += REWARD_LOSS;
        else if (winner == DRAW)
            n->reward[play] += REWARD_DRAW;
        return winner;
    } else {
        /* Choose a random unplayed move. */
        play = random_play_from_remaining(n, m->rng);
        assert(play >= 0 && play <= 61);
        uint64_t next_state[2] = {n->state[0], n->state[1]};
        next_state[turn] |= UINT64_C(1) << play;
        uint64_t dummy;
        switch (check(next_state[turn], next_state[!turn], play, &dummy)) {
            case YAVALATH_GAME_WIN:
                n->playouts[play]++;
                n->total_playouts++;
                n->reward[play] += REWARD_WIN;
                n->next[play] = turn ? MCTS_WIN1 : MCTS_WIN0;
                n->unexplored--;
                return turn;
            case YAVALATH_GAME_LOSS:
                n->playouts[play]++;
                n->total_playouts++;
                n->reward[play] += REWARD_LOSS;
                n->next[play] = turn ? MCTS_WIN0 : MCTS_WIN1;
                n->unexplored--;
                return !turn;
            case YAVALATH_GAME_DRAW:
                n->playouts[play]++;
                n->total_playouts++;
                n->reward[play] += REWARD_DRAW;
                n->next[play] = MCTS_DRAW;
                n->unexplored--;
                return DRAW; // neither
            case YAVALATH_GAME_UNRESOLVED:
                n->next[play] = mcts_alloc(m, next_state);
                if (n->next[play] == MCTS_NULL)
                    return -1; // out of memory
                n->unexplored--;
                n->playouts[play]++;
                n->total_playouts++;
                break;
        }
        /* Simulate remaining without allocation. */
        int winner = mcts_playout_final(m->rng, next_state, turn);
        if (winner == turn)
            n->reward[play] += REWARD_WIN;
        else if (winner == !turn)
            n->reward[play] += REWARD_LOSS;
        else if (winner == DRAW)
            n->reward[play] += REWARD_DRAW;
        return winner;
    }
}

/* API */

int
yavalath_hex_to_bit(int q, int r)
{
    return hex_to_bit(q, r);
}

void
yavalath_bit_to_hex(int bit, int *q, int *r)
{
    bit_to_hex(bit, q, r);
}

int
yavalath_notation_to_bit(const char *notation)
{
    int q, r;
    if (notation_to_hex(notation, &q, &r))
        return hex_to_bit(q, r);
    return -1;
}

void
yavalath_bit_to_notation(char *notation, int bit)
{
    int q = 0;
    int r = 0;
    bit_to_hex(bit, &q, &r);
    hex_to_notation(notation, q, r);
}

enum yavalath_game_result
yavalath_check(uint64_t  who,
               uint64_t  opponent,
               int       bit,
               uint64_t *where)
{
    uint64_t dummy;
    if (!where)
        where = &dummy;
    return check(who, opponent, bit, where);
}

enum yavalath_result
yavalath_ai_init(void    *buf,
                 size_t   bufsize,
                 uint64_t player0,
                 uint64_t player1,
                 uint64_t seed)
{
    uint64_t state[2] = {player0, player1};
    if (player0 & player1)
        return YAVALATH_INVALID_ARGUMENT;
    if (player0 & UINT64_C(0xe000000000000000))
        return YAVALATH_INVALID_ARGUMENT;
    if (player1 & UINT64_C(0xe000000000000000))
        return YAVALATH_INVALID_ARGUMENT;
    if (mcts_init(buf, bufsize, state, 0, seed))
        return YAVALATH_SUCCESS;
    return YAVALATH_INVALID_ARGUMENT;
}

enum yavalath_result
yavalath_ai_advance(void *buf, int bit)
{
    if (mcts_advance(buf, bit))
        return YAVALATH_SUCCESS;
    return YAVALATH_INVALID_ARGUMENT;
}

enum yavalath_result
yavalath_ai_playout(void *buf, uint32_t num_playouts)
{
    struct mcts *m = buf;
    for (uint32_t i = 0; i < num_playouts; i++) {
        int r = mcts_playout(m, m->root, m->root_turn);
        if (r == -1)
            return YAVALATH_BAILOUT_MEMORY;
        else if (r == -2)
            return YAVALATH_BAILOUT_OVERFLOW;
    }
    return YAVALATH_SUCCESS;
}

int
yavalath_ai_best_move(void *buf)
{
    struct mcts *m = buf;
    struct mcts_node *n = m->nodes + m->root;
    uint64_t taken = n->state[0] | n->state[1];
    double best_ratio = -INFINITY;
    int best[61];
    int nbest = 0;
    for (int i = 0; i < 61; i++) {
        if (!((taken >> i) & 1) && n->playouts[i]) {
            double ratio = n->reward[i] / (double)n->playouts[i];
            if (ratio > best_ratio) {
                nbest = 1;
                best[0] = i;
                best_ratio = ratio;
            } else if (ratio == best_ratio) {
                best[nbest++] = i;
            }
        }
    }
    return nbest == 1 ? best[0] : best[xoroshiro128plus(m->rng) % nbest];
}

double
yavalath_ai_get_move_score(const void *buf, int bit)
{
    const struct mcts *m = buf;
    const struct mcts_node *n = m->nodes + m->root;
    uint64_t taken = n->state[0] | n->state[1];
    if (!((taken >> bit) & 1) && n->playouts[bit])
        return n->reward[bit] / (double)n->playouts[bit];
    return 0;
}

uint32_t
yavalath_ai_get_nodes_total(const void *buf)
{
    const struct mcts *m = buf;
    return m->nodes_avail;
}

uint32_t
yavalath_ai_get_nodes_used(const void *buf)
{
    const struct mcts *m = buf;
    return m->nodes_allocated;
}

uint32_t
yavalath_ai_get_total_playouts(const void *buf)
{
    const struct mcts *m = buf;
    return m->nodes[m->root].total_playouts;
}
