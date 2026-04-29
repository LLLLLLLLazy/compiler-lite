int g_a = 3;
int g_b = 7;
int g_c = 11;
int g_sel = 2;
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

int f(int sel, int a, int b, int c)
{
    int x;
    int y;
    int z;
    int t;

    x = a;
    y = b;
    z = c;

    if (sel == 0) {
    } else if (sel == 1) {
        t = x;
        x = y;
        y = t;
    } else if (sel == 2) {
        t = x;
        x = y;
        y = z;
        z = t;
    } else {
        t = x;
        x = z;
        z = t;
    }

    return enc(x, y, z);
}

int expect_value(int sel, int a, int b, int c)
{
    if (sel == 0) {
        return enc(a, b, c);
    }

    if (sel == 1) {
        return enc(b, a, c);
    }

    if (sel == 2) {
        return enc(b, c, a);
    }

    return enc(c, b, a);
}

int main()
{
    int a;
    int b;
    int c;
    int sel;

    a = g_a;
    b = g_b;
    c = g_c;
    sel = g_sel;

    check(f(0, a, b, c), enc(a, b, c));
    check(f(1, a, b, c), enc(b, a, c));
    check(f(2, a, b, c), enc(b, c, a));
    check(f(3, a, b, c), enc(c, b, a));
    check(f(sel, a, b, c), expect_value(sel, a, b, c));

    return fail;
}