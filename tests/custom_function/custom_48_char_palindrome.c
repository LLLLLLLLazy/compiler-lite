int main(){
    int a[20];
    int n = 0;
    int c;
    int ok = 1;
    c = getch();
    while (c != 10) {
        a[n] = c;
        n = n + 1;
        c = getch();
    }
    for (int i = 0; i < n / 2; i = i + 1)
        if (a[i] != a[n - 1 - i]) ok = 0;
    putint(ok); putch(10);
    return 0;
}
