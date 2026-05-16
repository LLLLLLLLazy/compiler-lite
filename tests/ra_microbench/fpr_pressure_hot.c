int main()
{
    float f0 = 1.0;
    float f1 = 2.0;
    float f2 = 3.0;
    float f3 = 4.0;
    float f4 = 5.0;
    float f5 = 6.0;
    float f6 = 7.0;
    float f7 = 8.0;
    float f8 = 9.0;
    float f9 = 10.0;
    float f10 = 11.0;
    float f11 = 12.0;
    int i = 0;
    starttime();
    while (i < 3000) {
        f0 = f0 + f6 * 0.01;
        f1 = f1 + f7 * 0.01;
        f2 = f2 + f8 * 0.01;
        f3 = f3 + f9 * 0.01;
        f4 = f4 + f10 * 0.01;
        f5 = f5 + f11 * 0.01;
        f6 = f6 + f0 * 0.01;
        f7 = f7 + f1 * 0.01;
        f8 = f8 + f2 * 0.01;
        f9 = f9 + f3 * 0.01;
        f10 = f10 + f4 * 0.01;
        f11 = f11 + f5 * 0.01;
        i = i + 1;
    }
    stoptime();
    int ok = 0;
    if (f0 > 1.0) {
        if (f11 > 12.0) {
            ok = 1;
        }
    }
    putint(ok);
    putch(10);
    return 0;
}
