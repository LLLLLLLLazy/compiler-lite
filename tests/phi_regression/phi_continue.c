/* phi_continue: loop with a continue statement.
 * Decrement n first; skip swap via continue if n is even, else swap x/y.
 * Tests that the loop-header phi handles 3 predecessors correctly:
 *   (1) initial entry, (2) continue back-edge, (3) normal back-edge. */

int g_n = 5;
int g_a = 3;
int g_b = 7;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int enc(int x, int y)
{
    return x * 1000 + y;
}

int f(int n, int a, int b)
{
    int x;
    int y;
    int t;

    x = a;
    y = b;

    while (n > 0) {
        n = n - 1;
        if (n % 2 == 0) {
            continue;
        }
        t = x;
        x = y;
        y = t;
    }

    return enc(x, y);
}

/* swaps happen at odd values of (n after decrement): n/2 total swaps */
int expect_value(int n, int a, int b)
{
    int swaps;

    swaps = n / 2;
    if (swaps % 2) {
        return enc(b, a);
    }
    return enc(a, b);
}

int main()
{
    int a;
    int b;
    int n;

    a = g_a;
    b = g_b;
    n = g_n;

    check(f(0, a, b), enc(a, b));
    check(f(3, a, b), enc(b, a));
    check(f(4, a, b), enc(a, b));
    check(f(6, a, b), enc(b, a));
    check(f(n, a, b), expect_value(n, a, b));

    return fail;
}
