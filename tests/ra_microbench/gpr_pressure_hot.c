int main()
{
    int a0 = 1;
    int a1 = 2;
    int a2 = 3;
    int a3 = 4;
    int a4 = 5;
    int a5 = 6;
    int a6 = 7;
    int a7 = 8;
    int a8 = 9;
    int a9 = 10;
    int a10 = 11;
    int a11 = 12;
    int a12 = 13;
    int a13 = 14;
    int a14 = 15;
    int a15 = 16;
    int a16 = 17;
    int a17 = 18;
    int a18 = 19;
    int a19 = 20;
    int a20 = 21;
    int a21 = 22;
    int a22 = 23;
    int a23 = 24;
    int a24 = 25;
    int a25 = 26;
    int a26 = 27;
    int a27 = 28;
    int i = 0;
    starttime();
    while (i < 3000) {
        a0 = a0 + a14;
        a1 = a1 + a15;
        a2 = a2 + a16;
        a3 = a3 + a17;
        a4 = a4 + a18;
        a5 = a5 + a19;
        a6 = a6 + a20;
        a7 = a7 + a21;
        a8 = a8 + a22;
        a9 = a9 + a23;
        a10 = a10 + a24;
        a11 = a11 + a25;
        a12 = a12 + a26;
        a13 = a13 + a27;
        a14 = a14 + a0;
        a15 = a15 + a1;
        a16 = a16 + a2;
        a17 = a17 + a3;
        a18 = a18 + a4;
        a19 = a19 + a5;
        a20 = a20 + a6;
        a21 = a21 + a7;
        a22 = a22 + a8;
        a23 = a23 + a9;
        a24 = a24 + a10;
        a25 = a25 + a11;
        a26 = a26 + a12;
        a27 = a27 + a13;
        i = i + 1;
    }
    stoptime();
    int ok = 0;
    int total = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;
    total = total + a10 + a11 + a12 + a13 + a14 + a15 + a16 + a17 + a18 + a19;
    total = total + a20 + a21 + a22 + a23 + a24 + a25 + a26 + a27;
    if (total != 0) {
        ok = 1;
    }
    putint(ok);
    putch(10);
    return 0;
}
