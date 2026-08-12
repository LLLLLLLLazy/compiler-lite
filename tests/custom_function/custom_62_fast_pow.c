int powmod(int base, int exp, int mod) {
    int result = 1;
    while (exp > 0) {
        if (exp % 2 == 1) result = result * base % mod;
        base = base * base % mod;
        exp = exp / 2;
    }
    return result;
}
int main(){
    putint(powmod(2, 10, 1000)); putch(10);
    putint(powmod(3, 5, 7)); putch(10);
    putint(powmod(2, 20, 97)); putch(10);
    putint(powmod(5, 0, 10)); putch(10);
    return 0;
}
