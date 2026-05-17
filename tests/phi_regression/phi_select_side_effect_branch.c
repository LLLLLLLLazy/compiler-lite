/* phi_select_side_effect_branch:
 * Converting a merge phi to select must not delete branch blocks that still
 * contain observable work such as global stores. */

int g_total;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int choose_and_touch(int c)
{
    int x;

    if (c) {
        g_total = g_total + 3;
        x = 11;
    } else {
        g_total = g_total + 5;
        x = 22;
    }

    return x;
}

int main()
{
    check(choose_and_touch(1), 11);
    check(g_total, 3);

    check(choose_and_touch(0), 22);
    check(g_total, 8);

    return fail;
}
