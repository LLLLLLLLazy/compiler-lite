int g_a = 1;
int g_b = 2;
int g_c = 3;
int g_d = 4;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int enc(int w, int x, int y, int z)
{
    return w * 1000 + x * 100 + y * 10 + z;
}

int f(int cnd, int a, int b, int c, int d)
{
    int w;
    int x;
    int y;
    int z;
    int t1;
    int t2;

    w = a;
    x = b;
    y = c;
    z = d;

    if (cnd) {
        t1 = w;
        w = x;
        x = t1;

        t2 = y;
        y = z;
        z = t2;
    }

    return enc(w, x, y, z);
}

int main()
{
    int a;
    int b;
    int c;
    int d;

    a = g_a;
    b = g_b;
    c = g_c;
    d = g_d;

    check(f(0, a, b, c, d), enc(a, b, c, d));
    check(f(1, a, b, c, d), enc(b, a, d, c));

    return fail;
}