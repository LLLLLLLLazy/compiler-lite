/* phi_basic: minimal real phi-lowering hazards.
 * lost_copy_if creates merge phis whose then-edge copies are effectively:
 *   x_join <- y_then
 *   z_join <- x_then
 * swap_if creates merge phis whose then-edge copies are effectively:
 *   x_join <- y_then
 *   y_join <- x_then
 * These cover the two classic failure modes of phi destruction:
 * lost-copy and swap-cycle. */

int g_a = 3;
int g_b = 7;
int g_c = 11;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int enc2(int x, int y)
{
    return x * 1000 + y;
}

int enc3(int x, int y, int z)
{
    return x * 1000000 + y * 1000 + z;
}

int lost_copy_if(int c, int a, int b, int d)
{
    int x;
    int y;
    int z;
    int old_x;

    x = a;
    y = b;
    z = d;

    if (c) {
        old_x = x;
        x = b;
        z = old_x;
    }

    return enc3(x, y, z);
}

int swap_if(int c, int a, int b)
{
    int x;
    int y;
    int t;

    x = a;
    y = b;

    if (c) {
        t = x;
        x = y;
        y = t;
    }

    return enc2(x, y);
}

int main()
{
    int a;
    int b;
    int c;

    a = g_a;
    b = g_b;
    c = g_c;

    check(lost_copy_if(0, a, b, c), enc3(a, b, c));
    check(lost_copy_if(1, a, b, c), enc3(b, b, a));

    check(swap_if(0, a, b), enc2(a, b));
    check(swap_if(1, a, b), enc2(b, a));

    return fail;
}