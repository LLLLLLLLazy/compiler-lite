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

    a = abs_like(-1.25);
    b = keep_positive(-0.5);
    c = keep_positive(0.75);

    check(a > 1.2, 1);
    check(a < 1.3, 1);
    check(b == 0.0, 1);
    check(c > 0.7, 1);
    check(c < 0.8, 1);

    return fail;
}