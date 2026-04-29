/* phi_nested_accum: nested loops with an outer loop-carried accumulator.
 * Tests that SCCP does not prematurely fold the accumulator phi to the
 * entry constant before the inner-loop backedge becomes executable. */

int g_n = 3;
int g_m = 2;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int n, int m)
{
    int s;
    int i;
    int j;

    s = 0;
    i = 0;

    while (i < n) {
        j = 0;
        while (j < m) {
            s = s + 1;
            j = j + 1;
        }
        i = i + 1;
    }

    return s;
}

int main()
{
    int n;
    int m;

    n = g_n;
    m = g_m;

    check(f(0, 0), 0);
    check(f(0, 3), 0);
    check(f(1, 0), 0);
    check(f(1, 2), 2);
    check(f(3, 2), 6);
    check(f(4, 3), 12);
    check(f(n, m), n * m);

    return fail;
}