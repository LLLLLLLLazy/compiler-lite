/* phi_both_consts: phi node where BOTH incoming values are compile-time constants.
 * if-branch assigns 3, else-branch assigns 7.
 * phi-lowering must emit a constant-move in EACH predecessor block (no variable copy).
 * Tests that constant phi operands are handled correctly during lowering and register
 * allocation (no stale register reference for a constant-valued incoming). */

int g_sel = 1;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int c)
{
    int x;

    if (c) {
        x = 3;
    } else {
        x = 7;
    }

    return x;
}

/* Two-variable version: both x and y have constant-only phi incomings. */
int g(int c)
{
    int x;
    int y;

    if (c) {
        x = 10;
        y = 20;
    } else {
        x = 30;
        y = 40;
    }

    return x * 100 + y;
}

int main()
{
    int sel;

    sel = g_sel;

    check(f(0), 7);
    check(f(1), 3);
    check(f(sel), 3);

    check(g(0), 30 * 100 + 40);
    check(g(1), 10 * 100 + 20);
    check(g(sel), 10 * 100 + 20);

    return fail;
}
