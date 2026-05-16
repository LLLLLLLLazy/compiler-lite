int main()
{
    int i = 0;
    int isum0 = 1;
    int isum1 = 2;
    int isum2 = 3;
    int isum3 = 4;
    float fsum0 = 1.0;
    float fsum1 = 2.0;
    float fsum2 = 3.0;
    float fsum3 = 4.0;
    starttime();
    while (i < 5000) {
        isum0 = isum0 + isum2;
        isum1 = isum1 + isum3;
        isum2 = isum2 + isum0;
        isum3 = isum3 + isum1;
        fsum0 = fsum0 + fsum2 * 0.001;
        fsum1 = fsum1 + fsum3 * 0.001;
        fsum2 = fsum2 + fsum0 * 0.001;
        fsum3 = fsum3 + fsum1 * 0.001;
        i = i + 1;
    }
    stoptime();
    int ok = 0;
    if (isum0 != 0) {
        if (fsum3 > 4.0) {
            ok = 1;
        }
    }
    putint(ok);
    putch(10);
    return 0;
}
