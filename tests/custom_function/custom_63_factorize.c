int main(){
    int n = 48;
    int d = 2;
    while (n > 1) {
        if (n % d == 0) {
            putint(d);
            putch(32);
            n = n / d;
        } else {
            d = d + 1;
        }
    }
    putch(10);
    n = 97;
    d = 2;
    while (n > 1) {
        if (n % d == 0) {
            putint(d);
            putch(32);
            n = n / d;
        } else {
            d = d + 1;
        }
    }
    putch(10);
    return 0;
}
