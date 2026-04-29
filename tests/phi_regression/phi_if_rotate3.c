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

int enc(int x, int y, int z)
{
    return x * 10000 + y * 100 + z;
}

int f(int c, int a, int b, int d)
{
    int x;
    int y;
    int z;
    int t;

    x = a;
    y = b;
    z = d;

    if (c) {
        t = x;
        x = y;
        y = z;
        z = t;
    }

    return enc(x, y, z);
}

int main()
{
    int a;
    int b;
    int c;

    a = g_a;
    b = g_b;
    c = g_c;

    check(f(0, a, b, c), enc(a, b, c));
    check(f(1, a, b, c), enc(b, c, a));

    return fail;
}