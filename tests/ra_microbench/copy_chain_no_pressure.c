int main()
{
    int a = 1;
    int b = 2;
    int i = 0;
    starttime();
    while (i < 8000) {
        int t0 = a;
        int t1 = t0;
        int t2 = t1;
        a = b;
        b = t2;
        i = i + 1;
    }
    stoptime();
    putint(a + b);
    putch(10);
    return 0;
}
