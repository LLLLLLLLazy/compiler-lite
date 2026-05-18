static int hidden_counter = 6;
static int hidden_step = 4;
int fail;

void check(int got, int expect)
{
    if (got != expect) {
        fail = fail + 1;
    }
}

int bump_hidden()
{
    hidden_counter = hidden_counter + hidden_step;
    return hidden_counter;
}

int main()
{
    check(bump_hidden(), 10);
    check(bump_hidden(), 14);
    return fail;
}
