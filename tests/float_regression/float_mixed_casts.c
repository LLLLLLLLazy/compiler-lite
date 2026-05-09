int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int main()
{
    int i;
    float x;
    float y;
    int cmp;

    i = 3;
    x = i;
    y = x / 2.0;

    cmp = 0;
    if (x == 3.0) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (y > 1.4) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (y < 1.6) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (i < 3.5) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (4 > y) {
        cmp = 1;
    }
    check(cmp, 1);

    return fail;
}