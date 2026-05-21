int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int next_value()
{
    static int x = 3;

    x = x + 2;
    return x;
}

int main()
{
    check(next_value(), 5);
    check(next_value(), 7);
    check(next_value(), 9);
    return fail;
}
