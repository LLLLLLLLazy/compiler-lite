/* phi_select_zero_compare:
 * A select fed by (value == 0) must use the materialized boolean condition,
 * not reread a compared operand after later instructions have reused registers. */

int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int add_if_even_product(int a, int b, int sum)
{
    int prod;
    int rem;
    int next;

    prod = a * b;
    rem = prod % 2;
    next = sum;
    if (rem == 0) {
        next = sum + prod;
    }

    return next;
}

int main()
{
    check(add_if_even_product(3, 4, 7), 19);
    check(add_if_even_product(3, 5, -15), -15);
    return fail;
}
