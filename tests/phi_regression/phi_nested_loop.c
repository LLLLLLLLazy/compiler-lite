/* phi_nested_loop: nested while loops sharing x/y variables.
 * Inner loop swaps x,y m times; outer loop iterates n times.
 * Total swaps = n*m; result parity determines final (x,y) order.
 * Tests that phi nodes at two loop-header levels are lowered correctly. */

int g_n = 3;
int g_m = 2;
int g_a = 1;
int g_b = 10;
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

int f(int n, int m, int a, int b)
{
    int x;
    int y;
    int t;
    int i;
    int j;

    x = a;
    y = b;
    i = 0;

    while (i < n) {
        j = 0;
        while (j < m) {
            t = x;
            x = y;
            y = t;
            j = j + 1;
        }
        i = i + 1;
    }

    return enc(x, y);
}

int expect_value(int n, int m, int a, int b)
{
    int total;

    total = n * m;
    if (total % 2) {
        return enc(b, a);
    }
    return enc(a, b);
}

int main()
{
    int a;
    int b;
    int n;
    int m;

    a = g_a;
    b = g_b;
    n = g_n;
    m = g_m;

    check(f(0, 0, a, b), enc(a, b));
    check(f(0, 1, a, b), enc(a, b));
    check(f(1, 0, a, b), enc(a, b));
    check(f(1, 1, a, b), enc(b, a));
    check(f(1, 2, a, b), enc(a, b));
    check(f(2, 1, a, b), enc(a, b));
    check(f(2, 3, a, b), enc(a, b));
    check(f(n, m, a, b), expect_value(n, m, a, b));

    return fail;
}
