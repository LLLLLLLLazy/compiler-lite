/* phi_loop_accum_swap: loop-carried phi with an accumulator that depends on a swap cycle.
 * Each iteration: s += x_old, then swap x <-> y using a temp.
 * Critical dependency: s must read x_old BEFORE x is overwritten by the swap.
 * Briggs-Cooper phi-lowering must serialize the copies so that:
 *   (1) the copy "s = s + x" reads x before its swap copy fires,
 *   (2) the x <-> y swap cycle is broken with a temporary.
 * Iteration pattern: adds a, b, a, b, ... (alternating a and b)
 * For n iterations: s = ceil(n/2)*a + floor(n/2)*b */

int g_a = 3;
int g_b = 7;
int g_n = 6;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int n, int a, int b)
{
    int x;
    int y;
    int s;
    int t;
    int i;

    x = a;
    y = b;
    s = 0;
    i = 0;

    while (i < n) {
        s = s + x;
        t = x;
        x = y;
        y = t;
        i = i + 1;
    }

    return s;
}

/* expect_s(n, a, b) = ceil(n/2)*a + floor(n/2)*b */
int expect_s(int n, int a, int b)
{
    int half;
    int rem;

    half = n / 2;
    rem = n % 2;

    return half * a + half * b + rem * a;
}

int main()
{
    int a;
    int b;
    int n;

    a = g_a;
    b = g_b;
    n = g_n;

    check(f(0, a, b), 0);
    check(f(1, a, b), a);
    check(f(2, a, b), a + b);
    check(f(3, a, b), 2 * a + b);
    check(f(4, a, b), 2 * a + 2 * b);
    check(f(5, a, b), 3 * a + 2 * b);
    check(f(n, a, b), expect_s(n, a, b));

    return fail;
}
