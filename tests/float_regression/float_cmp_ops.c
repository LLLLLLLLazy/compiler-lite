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
    int cmp;

    a = 1.5;
    b = 2.5;
    i = 2;

    cmp = 0;
    if (a < b) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (a > b) {
        cmp = 1;
    }
    check(cmp, 0);

    cmp = 0;
    if (a <= 1.5) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (b >= i) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (a == 1.5) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (a != b) {
        cmp = 1;
    }
    check(cmp, 1);

    return fail;
}