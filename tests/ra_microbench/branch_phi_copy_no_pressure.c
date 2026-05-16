int main()
{
    int i = 0;
    int sum = 0;
    starttime();
    while (i < 8000) {
        int v;
        if (i % 2) {
            v = i + 1;
        } else {
            v = i + 2;
        }
        sum = sum + v;
        i = i + 1;
    }
    stoptime();
    putint(sum);
    putch(10);
    return 0;
}
