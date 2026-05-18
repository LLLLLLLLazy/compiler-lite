int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int touch_array(int index, int delta)
{
    static int values[4] = {1, 2, 3, 4};

    values[index] = values[index] + delta;
    return values[0] * 1000 + values[1] * 100 + values[2] * 10 + values[3];
}

int main()
{
    check(touch_array(1, 5), 1734);
    check(touch_array(2, 6), 1794);
    check(touch_array(1, 1), 1894);
    return fail;
}
