int g_a = 3;
int g_b = 7;
int g_n = 5;
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
        t = x;
        x = y;
        y = t;
        n = n - 1;
    }

    return enc(x, y);
}

int expect_value(int n, int a, int b)
{
    if (n % 2) {
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

    check(f(0, a, b), expect_value(0, a, b));
    check(f(1, a, b), expect_value(1, a, b));
    check(f(2, a, b), expect_value(2, a, b));
    check(f(n, a, b), expect_value(n, a, b));

    return fail;
}