int main()
{
    int x = 1;
    int i = 0;
    starttime();
    while (i < 8000) {
        x = x + 3;
        i = i + 1;
    }
    stoptime();
    putint(x);
    putch(10);
    return 0;
}
