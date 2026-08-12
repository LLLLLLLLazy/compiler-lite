int main(){
    int n = 42;
    int bits[20];
    int k = 0;
    while (n > 0) {
        bits[k] = n % 2;
        n = n / 2;
        k = k + 1;
    }
    for (int i = k - 1; i >= 0; i = i - 1) {
        putint(bits[i]);
    }
    putch(10);
    n = 255;
    int oct[10];
    k = 0;
    while (n > 0) {
        oct[k] = n % 8;
        n = n / 8;
        k = k + 1;
    }
    for (int i = k - 1; i >= 0; i = i - 1) {
        putint(oct[i]);
    }
    putch(10);
    return 0;
}
