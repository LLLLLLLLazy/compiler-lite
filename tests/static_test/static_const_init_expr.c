int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int calc()
{
    static const int base = 2 + 3 * 4;
    static int value = (base - 1) * 2;

    value = value + base;
    return value;
}

int main()
{
    check(calc(), 40);
    check(calc(), 54);
    return fail;
}
