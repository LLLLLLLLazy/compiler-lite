int isPrime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i = i + 1) {
        if (n % i == 0) return 0;
    }
    return 1;
}
int main(){
    int sum = 0;
    for (int i = 2; i < 100; i = i + 1)
        if (isPrime(i)) sum = sum + 1;
    putint(sum); putch(10);
    if (isPrime(97)) putint(1); else putint(0); putch(10);
    if (isPrime(91)) putint(1); else putint(0); putch(10);
    if (isPrime(2)) putint(1); else putint(0); putch(10);
    return 0;
}
