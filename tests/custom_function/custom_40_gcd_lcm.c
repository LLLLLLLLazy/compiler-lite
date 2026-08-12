int gcd(int a, int b) {
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}
int lcm(int a, int b) {
    return a / gcd(a, b) * b;
}
int main(){
    putint(gcd(48, 36)); putch(10);
    putint(gcd(17, 5)); putch(10);
    putint(gcd(0, 9)); putch(10);
    putint(lcm(4, 6)); putch(10);
    putint(lcm(7, 9)); putch(10);
    putint(gcd(gcd(12, 18), 30)); putch(10);
    return 0;
}
