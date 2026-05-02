/* phi_phi_chain: nested if-else creating a phi-feeding-phi dependency chain.
 * When p is true, x is set inside a nested if-else (inner phi: b or c).
 * When p is false, x keeps its initial value a.
 * Outer merge phi uses the RESULT OF the inner merge phi as one incoming:
 *   x_outer = phi(x_inner from p_then, a from p_else)
 *   x_inner = phi(b from q_then, c from q_else)
 * Tests that phi lowering correctly handles a phi whose incoming value is
 * itself a phi instruction (the chain must be serialized safely). */

int g_a = 2;
int g_b = 5;
int g_c = 9;
int g_p = 1;
int g_q = 0;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int f(int p, int q, int a, int b, int c)
{
    int x;

    x = a;
    if (p) {
        if (q) {
            x = b;
        } else {
            x = c;
        }
    }

    return x;
}

/* Three-level chain: p -> q -> r */
int h(int p, int q, int r, int a, int b, int c, int d)
{
    int x;

    x = a;
    if (p) {
        if (q) {
            if (r) {
                x = d;
            } else {
                x = c;
            }
        } else {
            x = b;
        }
    }

    return x;
}

int main()
{
    int a;
    int b;
    int c;
    int p;
    int q;

    a = g_a;
    b = g_b;
    c = g_c;
    p = g_p;
    q = g_q;

    check(f(0, 0, a, b, c), a);
    check(f(0, 1, a, b, c), a);
    check(f(1, 0, a, b, c), c);
    check(f(1, 1, a, b, c), b);
    check(f(p, q, a, b, c), c);

    check(h(0, 0, 0, a, b, c, 99), a);
    check(h(1, 0, 0, a, b, c, 99), b);
    check(h(1, 1, 0, a, b, c, 99), c);
    check(h(1, 1, 1, a, b, c, 99), 99);

    return fail;
}
