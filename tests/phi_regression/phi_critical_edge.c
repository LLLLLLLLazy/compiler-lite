int g_a = 3;
int g_b = 7;
int g_p = 1;
int g_q = 0;
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

int f(int p, int q, int a, int b)
{
    int x;
    int y;
    int t;

    x = a;
    y = b;

    if (p) {
        x = x + 1;
        y = y + 2;

        if (q) {
            t = x;
            x = y;
            y = t;
        }
    } else {
        t = x;
        x = y + 3;
        y = t + 4;
    }

    return enc(x, y);
}

int expect_value(int p, int q, int a, int b)
{
    if (p) {
        if (q) {
            return enc(b + 2, a + 1);
        }

        return enc(a + 1, b + 2);
    }

    return enc(b + 3, a + 4);
}

int main()
{
    int a;
    int b;
    int p;
    int q;

    a = g_a;
    b = g_b;
    p = g_p;
    q = g_q;

    check(f(1, 0, a, b), enc(a + 1, b + 2));
    check(f(1, 1, a, b), enc(b + 2, a + 1));
    check(f(0, 0, a, b), enc(b + 3, a + 4));
    check(f(0, 1, a, b), enc(b + 3, a + 4));
    check(f(p, q, a, b), expect_value(p, q, a, b));

    return fail;
}