float bump(float x)
{
    return x * 1.001 + 0.25;
}

int main()
{
    float a = 1.0;
    float b = 2.0;
    float c = 3.0;
    float d = 4.0;
    float e = 5.0;
    float f = 6.0;
    int i = 0;
    starttime();
    while (i < 2000) {
        a = bump(a) + b;
        b = bump(b) + c;
        c = bump(c) + d;
        d = bump(d) + e;
        e = bump(e) + f;
        f = bump(f) + a;
        i = i + 1;
    }
    stoptime();
    int ok = 0;
    if (a > 0.0) {
        if (f > 0.0) {
            ok = 1;
        }
    }
    putint(ok);
    putch(10);
    return 0;
}
