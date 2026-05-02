/* phi_break_and_continue: a single loop that uses BOTH break AND continue.
 * This exercises phis at two different join points simultaneously:
 *   - Loop-header phi: has 3 predecessors (pre-loop entry, continue back-edge,
 *                      normal back-edge from the bottom of the loop body).
 *   - Post-loop phi:   has 2 predecessors (loop-condition false, break edge).
 * The accumulated sum 's' is loop-carried and also flows to the post-loop merge.
 * Tests that phi nodes for 's' and 'i' are independently correct at both join points. */

int g_n = 10;
int g_skip = 3;
int g_limit = 7;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

/* Sum i from 0..n-1, skipping i when (i % skip == 0), breaking when s + i > limit. */
int f(int n, int skip, int limit)
{
    int s;
    int i;

    s = 0;
    i = 0;

    while (i < n) {
        if (i % skip == 0) {
            i = i + 1;
            continue;
        }
        if (s + i > limit) {
            break;
        }
        s = s + i;
        i = i + 1;
    }

    return s;
}

int main()
{
    int n;
    int skip;
    int limit;

    n = g_n;
    skip = g_skip;
    limit = g_limit;

    /* Loop never entered */
    check(f(0, 3, 7), 0);

    /* Large limit: never breaks; skip multiples of 3 from 0..9
     * Skipped: 0,3,6,9.  Added: 1,2,4,5,7,8 = 27 */
    check(f(10, 3, 100), 27);

    /* limit=7: 1+2=3, +4=7, next (i=5): s+i=12 > 7 -> break; s=7 */
    check(f(10, 3, 7), 7);

    /* skip=1: every i skipped (i%1==0 always true), s stays 0 */
    check(f(10, 1, 100), 0);

    check(f(n, skip, limit), 7);

    return fail;
}
