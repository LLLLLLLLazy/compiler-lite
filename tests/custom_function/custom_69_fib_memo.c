int memo[31];
int fib(int n) {
    if (n < 2) return n;
    if (memo[n] != -1) return memo[n];
    memo[n] = fib(n - 1) + fib(n - 2);
    return memo[n];
}
int main(){
    for (int i = 0; i < 31; i = i + 1) memo[i] = -1;
    putint(fib(30)); putch(10);
    putint(fib(0)); putch(10);
    putint(fib(1)); putch(10);
    return 0;
}
