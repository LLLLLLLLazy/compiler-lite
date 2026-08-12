int main(){
    int found = 0;
    for (int n = 2; n <= 500; n = n + 1) {
        int s = 0;
        for (int d = 1; d < n; d = d + 1)
            if (n % d == 0) s = s + d;
        if (s == n) {
            putint(n);
            putch(32);
            found = found + 1;
        }
    }
    putch(10);
    putint(found); putch(10);
    return 0;
}
