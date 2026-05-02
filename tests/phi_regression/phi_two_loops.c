/* phi_two_loops: two sequential while loops that share the same variables.
 * The first loop increments x by 1 each iteration (n times).
 * The second loop increments x by 2 each iteration (m times), reusing i.
 * Tests that Mem2Reg places phi nodes independently for each loop header,
 * and that the value of x and i after the first loop feed correctly into
 * the second loop's entry phi (the "post-loop value becomes pre-loop value"
 * across two separate induction loops). */

int g_n = 3;
int g_m = 4;
int g_a = 10;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

/* f(n, m, a) = a + n + 2 * m */
int f(int n, int m, int a)
{
    int x;
    int i;

    x = a;
    i = 0;
    while (i < n) {
        x = x + 1;
        i = i + 1;
    }

    i = 0;
    while (i < m) {
        x = x + 2;
        i = i + 1;
    }

    return x;
}

/* g: same but with TWO separate accumulation variables each updated in alternating loops */
int g(int n, int a, int b)
{
    int x;
    int y;
    int i;

    x = a;
    y = b;

    i = 0;
    while (i < n) {
        x = x + y;
        i = i + 1;
    }

    i = 0;
    while (i < n) {
        y = y + x;
        i = i + 1;
    }

    return x * 1000 + y;
}

int main()
{
    int a;
    int n;
    int m;

    a = g_a;
    n = g_n;
    m = g_m;

    check(f(0, 0, a), a);
    check(f(3, 0, a), a + 3);
    check(f(0, 4, a), a + 8);
    check(f(3, 4, a), a + 11);
    check(f(n, m, a), a + n + 2 * m);

    /* g(1, 1, 2): after loop1 x=1+2=3, after loop2 y=2+3=5 -> 3*1000+5=3005 */
    check(g(1, 1, 2), 3 * 1000 + 5);
    /* g(0, 5, 7): no loops run, x=5, y=7 -> 5007 */
    check(g(0, 5, 7), 5 * 1000 + 7);

    return fail;
}
