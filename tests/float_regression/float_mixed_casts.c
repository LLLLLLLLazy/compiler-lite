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

    i = 3;
    x = i;
    y = x / 2.0;

    check(x == 3.0, 1);
    check(y > 1.4, 1);
    check(y < 1.6, 1);
    check(i < 3.5, 1);
    check(4 > y, 1);

    return fail;
}