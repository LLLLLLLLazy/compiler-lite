int main()
{
    int a = 1;
    int b = 2;
    int i = 0;
    starttime();
    while (i < 6000) {
        a = a + b;
        b = b + 1;
        i = i + 1;
    }
    stoptime();
    int ok = 0;
    if (a != 0) {
        ok = 1;
    }
    putint(ok);
    putch(10);
    return 0;
}
