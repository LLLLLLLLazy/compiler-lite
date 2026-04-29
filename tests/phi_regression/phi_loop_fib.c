/* phi_loop_fib: Fibonacci sequence via loop-carried accumulation.
 * a, b = b, a+b each iteration (using a temporary).
 * Tests that phi nodes with computed (non-permutation) incoming values
 * are lowered correctly — b_new depends on t = a_old + b_old. */

int g_n = 7;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

/* fib(0)=0, fib(1)=1, fib(2)=1, fib(3)=2, fib(4)=3, fib(5)=5, fib(6)=8, fib(7)=13 */
int fib(int n)
{
    int a;
    int b;
    int t;

    a = 0;
    b = 1;

    while (n > 0) {
        t = a + b;
        a = b;
        b = t;
        n = n - 1;
    }

    return a;
}

int main()
{
    int n;

    n = g_n;

    check(fib(0), 0);
    check(fib(1), 1);
    check(fib(2), 1);
    check(fib(3), 2);
    check(fib(4), 3);
    check(fib(5), 5);
    check(fib(6), 8);
    check(fib(n), 13);

    return fail;
}
