int g_a = 3;
int g_b = 7;
int g_c = 11;
int g_n = 5;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int enc(int x, int y, int z)
{
    return x * 10000 + y * 100 + z;
}

int f(int n, int a, int b, int c)
{
    int x;
    int y;
    int z;
    int t;

    x = a;
    y = b;
    z = c;

    while (n > 0) {
        t = x;
        x = y;
        y = z;
        z = t;
        n = n - 1;
    }

    return enc(x, y, z);
}

int expect_value(int n, int a, int b, int c)
{
    int r;

    r = n % 3;

    if (r == 0) {
        return enc(a, b, c);
    }

    if (r == 1) {
        return enc(b, c, a);
    }

    return enc(c, a, b);
}

int main()
{
    int a;
    int b;
    int c;
    int n;

    a = g_a;
    b = g_b;
    c = g_c;
    n = g_n;

    check(f(0, a, b, c), expect_value(0, a, b, c));
    check(f(1, a, b, c), expect_value(1, a, b, c));
    check(f(2, a, b, c), expect_value(2, a, b, c));
    check(f(3, a, b, c), expect_value(3, a, b, c));
    check(f(n, a, b, c), expect_value(n, a, b, c));

    return fail;
}