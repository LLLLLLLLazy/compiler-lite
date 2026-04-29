int g_a = 3;
int g_b = 7;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int c, int a, int b)
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

    return x * 1000 + y;
}

int main()
{
    int a;
    int b;

    a = g_a;
    b = g_b;

    check(f(0, a, b), a * 1000 + b);
    check(f(1, a, b), b * 1000 + a);

    return fail;
}