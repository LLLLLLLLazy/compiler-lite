int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

float abs_like(float x)
{
    if (x < 0.0) {
        return -x;
    }
    return x;
}

float keep_positive(float x)
{
    if (x > 0.0) {
        return x;
    }
    return 0.0;
}

int main()
{
    float a;
    float b;
    float c;
    int cmp;

    a = abs_like(-1.25);
    b = keep_positive(-0.5);
    c = keep_positive(0.75);

    cmp = 0;
    if (a > 1.2) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (a < 1.3) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (b == 0.0) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (c > 0.7) {
        cmp = 1;
    }
    check(cmp, 1);

    cmp = 0;
    if (c < 0.8) {
        cmp = 1;
    }
    check(cmp, 1);

    return fail;
}