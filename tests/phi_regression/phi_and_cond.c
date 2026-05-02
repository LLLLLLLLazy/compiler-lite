/* phi_and_cond: && short-circuit operator used as an if condition.
 * The && evaluation uses an alloca+store pattern that Mem2Reg promotes to a phi.
 * The phi for the && result has a constant-0 incoming on the short-circuit path
 * (when LHS is false, result is forced to 0 without evaluating RHS).
 * Additionally, the outer 'x' variable has a phi at the merge point.
 * Both phis exercise constant-incoming lowering. */

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

    x = 0;
    if (a > 0 && b > 0) {
        x = a + b;
    }

    return x;
}

/* Three-way && chain: a>0 && b>0 && c>0 */
int g(int a, int b, int c)
{
    int x;

    x = 0;
    if (a > 0 && b > 0 && c > 0) {
        x = a + b + c;
    }

    return x;
}

int main()
{
    int a;
    int b;

    a = g_a;
    b = g_b;

    check(f(3, 4), 7);
    check(f(0, 4), 0);
    check(f(3, 0), 0);
    check(f(0, 0), 0);
    check(f(a, b), a + b);

    check(g(1, 2, 3), 6);
    check(g(0, 2, 3), 0);
    check(g(1, 0, 3), 0);
    check(g(1, 2, 0), 0);

    return fail;
}
