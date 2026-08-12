int main(){
    int n = 12345;
    int digits[10];
    int k = 0;
    while (n > 0) {
        digits[k] = n % 10;
        n = n / 10;
        k = k + 1;
    }
    for (int i = k - 1; i >= 0; i = i - 1) putch(48 + digits[i]);
    putch(10);
    n = 7;
    putch(48 + n);
    putch(10);
    return 0;
}
