int state = 0;

int touch(int x)
{
    state = state + x;
    return x + 1;
}

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
    int i = 0;
    starttime();
    while (i < 2500) {
        int sum = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
        sum = sum + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15;
        i = touch(i);
        a0 = a0 + sum;
        a1 = a1 + sum;
        a2 = a2 + sum;
        a3 = a3 + sum;
        a4 = a4 + sum;
        a5 = a5 + sum;
        a6 = a6 + sum;
        a7 = a7 + sum;
        a8 = a8 + sum;
        a9 = a9 + sum;
        a10 = a10 + sum;
        a11 = a11 + sum;
        a12 = a12 + sum;
        a13 = a13 + sum;
        a14 = a14 + sum;
        a15 = a15 + sum;
    }
    stoptime();
    int ok = 0;
    int total = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    total = total + a8 + a9 + a10 + a11 + a12 + a13 + a14 + a15;
    if (total != 0) {
        ok = 1;
    }
    putint(ok);
    putch(10);
    return 0;
}
