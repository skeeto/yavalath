/**
 * Yavalath AI and Engine
 *
 * This AI represents the game state using two 64-bit bitboards, one
 * for each player's stones. As such, there are no fancy types to
 * declare and it is up to the caller to perform its own bit
 * operations, which are very simple.
 */
#include <stdint.h>

enum yavalath_game_result {
    YAVALATH_GAME_UNRESOLVED,
    YAVALATH_GAME_WIN,
    YAVALATH_GAME_LOSS,
    YAVALATH_GAME_DRAW,
};

enum yavalath_result {
    YAVALATH_SUCCESS = 0,
    YAVALATH_BAILOUT_OVERFLOW = 10,
    YAVALATH_BAILOUT_MEMORY,
    YAVALATH_INVALID_ARGUMENT,
};

/**
 * Convert axial coordinates to its bit.
 *
 * Returns -1 for invalid axial coordinates.
 * See: http://www.redblobgames.com/grids/hexagons/
 */
int
yavalath_hex_to_bit(int q,
                    int r);

/**
 * Convert bit to axial coordinates.
 *
 * The bit must be within [0 - 61).
 * See: http://www.redblobgames.com/grids/hexagons/
 */
void
yavalath_bit_to_hex(int  bit,
                    int *q,
                    int *r);

/**
 * Convert Susan notation to its bit.
 *
 * Returns -1 if notation is an invalid position.
 */
int
yavalath_notation_to_bit(const char *notation);

/**
 * Convert a bit to its Susan notation.
 *
 * The bit must be within [0 - 61), and exactly three bytes will be
 * written to the buffer.
 */
void
yavalath_bit_to_notation(char *notation,
                         int   bit);

/**
 * Returns the result of the given game.
 * who      : the acting player's stones
 * opponent : the opposing player's stones
 * bit      : the move that was made
 * where    : (output) bits relevant to the result, may be NULL
 *
 * Note: The game state is not validated, so there are no errors.
 */
enum yavalath_game_result
yavalath_check(uint64_t  who,
               uint64_t  opponent,
               int       bit,
               uint64_t *where);

/**
 * Initialize a buffer for use as a Yavalath AI.
 * buf     : the buffer
 * bufsize : total size of the buffer
 * player0 : stones for player 0 (current player)
 * player1 : stones for player 1
 * seed    : Monte Carlo seed (may be 0)
 *
 * The AI will make no other memory or resource allocations, and its
 * entire state will be stored in this buffer. This means the buffer
 * could be stored on disk (in an unportable format) and restarted
 * from the same point in the future. There is no need to deinitialize
 * this buffer when you're done.
 *
 * The bufsize must be at least several megabytes, typically several
 * gigabytes. Ideally it will be just large enough to avoid cutting
 * playouts short due to memory exhaustion.
 *
 * The player0 and player1 values must not have overlapping bits, nor
 * may the upper 3 bits be set.
 *
 * Different seeds will slightly change the AI's choices, leading to
 * different games for the same opponent moves. A seed of 0 is
 * perfectly valid, but this does not mean it will automatically
 * generate a seed.
 *
 * Possible return values:
 *   YAVALATH_SUCCESS
 *   YAVALATH_INVALID_ARGUMENT : bufsize too small, or invalid game state
 */
enum yavalath_result
yavalath_ai_init(void    *buf,
                 size_t   bufsize,
                 uint64_t player0,
                 uint64_t player1,
                 uint64_t seed);

/**
 * Advance the AI's internal game state forward.
 *
 * This would be the opponents move (on the opponent's turn), or the
 * move turned by the AI from `yavalath_ai_best_move()`. This function
 * keeps the AI in sync with the game being played. This frees some of
 * the AI's state, allowing it to be recycled for more playouts.
 *
 * Possible return avalues:
 *   YAVALATH_SUCCESS
 *   YAVALATH_INVALID_ARGUMENT : the move was invalid
 */
enum yavalath_result
yavalath_ai_advance(void *buf,
                    int   bit);

/**
 * Try to perform a given number of playouts.
 *
 * Early bailouts are not errors and are expected for high total
 * playout counts. However, if a bailout occurs, a hard constraint has
 * been reached and no more playouts can be performed from the current
 * game state. The game must advance at least one turn (releasing
 * resources) before more playouts are possible.
 *
 * Possible return values:
 *   YAVALATH_SUCCESS
 *   YAVALATH_BAILOUT_OVERFLOW : further playouts would overflow an integer
 *   YAVALATH_BAILOUT_MEMORY   : playouts halted due to out-of-memory
 */
enum yavalath_result
yavalath_ai_playout(void    *buf,
                    uint32_t num_playouts);

/**
 * Return the believed best move from the current game state.
 *
 * This may generate a random number, changing future results.
 */
int
yavalath_ai_best_move(void *buf);

/**
 * Return the score (higher is better) of the given move.
 *
 * Used to inspect the AI's opinion of all available moves. The
 * `yavalath_ai_best_move()` function could be implemented in terms of
 * this function. It returns the move with the highest score, breaking
 * ties randomly.
 */
double
yavalath_ai_get_move_score(const void *buf, int bit);

/**
 * Return the total number of nodes available to the AI.
 */
uint32_t
yavalath_ai_get_nodes_total(const void *buf);

/**
 * Return the number of nodes currently in use by the AI.
 *
 * Dividing this by the total number of nodes gives a close
 * approximation to the percentage of the AI buffer in use.
 */
uint32_t
yavalath_ai_get_nodes_used(const void *buf);

/**
 * Return the total number of playouts through the current root.
 */
uint32_t
yavalath_ai_get_total_playouts(const void *buf);
