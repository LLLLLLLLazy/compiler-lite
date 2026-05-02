int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int main()
{
    float a;
    float b;
    int i;

    a = 1.5;
    b = 2.5;
    i = 2;

    check(a < b, 1);
    check(a > b, 0);
    check(a <= 1.5, 1);
    check(b >= i, 1);
    check(a == 1.5, 1);
    check(a != b, 1);

    return fail;
}