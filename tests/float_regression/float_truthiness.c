int fail;
int side;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int hit()
{
    side = side + 1;
    return 1;
}

int main()
{
    float z;
    float nz;
    float t;
    int steps;

    z = 0.0;
    nz = 0.25;
    t = 2.0;
    steps = 0;

    check(!z, 1);
    check(!nz, 0);

    side = 0;
    if (z && hit()) {
        fail = fail + 1;
    }
    check(side, 0);

    if (nz || hit()) {
        check(side, 0);
    } else {
        fail = fail + 1;
    }

    while (t) {
        steps = steps + 1;
        t = t - 1.0;
    }
    check(steps, 2);

    return fail;
}