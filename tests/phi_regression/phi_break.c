/* phi_break: loop with a break statement.
 * When i reaches target, swap x/y and break.
 * Tests that phi nodes at the loop exit block correctly merge
 * the no-break path (x,y unchanged) and the break path (x,y swapped). */

int g_n = 10;
int g_target = 5;
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

int f(int n, int target, int a, int b)
{
    int x;
    int y;
    int t;
    int i;

    x = a;
    y = b;
    i = 0;

    while (i < n) {
        if (i == target) {
            t = x;
            x = y;
            y = t;
            break;
        }
        i = i + 1;
    }

    return enc(x, y);
}

int main()
{
    int a;
    int b;
    int n;
    int target;

    a = 3;
    b = 7;
    n = g_n;
    target = g_target;

    check(f(10, 5, a, b), enc(b, a));
    check(f(10, 99, a, b), enc(a, b));
    check(f(0, 5, a, b), enc(a, b));
    check(f(n, target, a, b), enc(b, a));

    return fail;
}
