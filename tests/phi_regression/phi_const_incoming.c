/* phi_const_incoming: phi node where one incoming value is a compile-time constant.
 * if-branch assigns literal 0, else-branch assigns x + 1 (computed from parameter).
 * Tests that phi-lowering correctly emits a constant move for the constant incoming
 * instead of a register-to-register copy. */

int g_x = 5;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int c, int x)
{
    int y;

    if (c) {
        y = 0;
    } else {
        y = x + 1;
    }

    return y;
}

int main()
{
    int x;

    x = g_x;

    check(f(1, 3), 0);
    check(f(0, 3), 4);
    check(f(1, 0), 0);
    check(f(0, 7), 8);
    check(f(0, x), x + 1);
    check(f(1, x), 0);

    return fail;
}
