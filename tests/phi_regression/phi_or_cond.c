/* phi_or_cond: || short-circuit operator used as an if condition.
 * The || evaluation uses an alloca+store pattern that Mem2Reg promotes to a phi.
 * The phi for the || result has a constant-1 incoming on the short-circuit path
 * (when LHS is true, result is forced to 1 without evaluating RHS).
 * Additionally, the outer 'x' variable has a phi at the merge point.
 * Exercises constant-1 incoming vs. the constant-0 in phi_and_cond. */

int g_a = 3;
int g_b = 4;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int a, int b)
{
    int x;

    x = 99;
    if (a > 0 || b > 0) {
        x = a + b;
    }

    return x;
}

/* Swap using ||: result depends on which path was taken */
int g(int a, int b)
{
    int x;
    int y;
    int t;

    x = a;
    y = b;
    if (!(a > 0 || b > 0)) {
        t = x;
        x = y;
        y = t;
    }

    return x * 1000 + y;
}

int main()
{
    int a;
    int b;

    a = g_a;
    b = g_b;

    check(f(3, 4), 7);
    check(f(0, 4), 4);
    check(f(3, 0), 3);
    check(f(0, 0), 99);
    check(f(a, b), a + b);

    /* g: swaps only when BOTH a<=0 and b<=0 */
    check(g(3, 4), 3 * 1000 + 4);
    check(g(0, 0), 0 * 1000 + 0);

    return fail;
}
