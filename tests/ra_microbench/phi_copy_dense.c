int main()
{
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int i = 0;
    starttime();
    while (i < 8000) {
        if (i % 2) {
            int t0 = a;
            int t1 = b;
            a = c;
            b = d;
            c = t0;
            d = t1;
        } else {
            int t2 = a;
            a = b;
            b = c;
            c = d;
            d = t2;
        }
        i = i + 1;
    }
    stoptime();
    putint(a + b + c + d);
    putch(10);
    return 0;
}
