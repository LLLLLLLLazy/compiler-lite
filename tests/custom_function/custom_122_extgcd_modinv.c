/* 扩展欧几里得 + 模逆元: 负余数处理与系数回代,
   输出 (a,b) 的 gcd 系数与多组模逆元, 含不可逆情形。 */
int xg;
int yg;
int extgcd(int a, int b) {
    if (b == 0) {
        xg = 1;
        yg = 0;
        return a;
    }
    int g = extgcd(b, a % b);
    int t = xg;
    xg = yg;
    yg = t - a / b * yg;
    return g;
}
int modinv(int a, int m) {
    int g = extgcd(a, m);
    if (g != 1) return -1;
    int r = xg % m;
    if (r < 0) r = r + m;
    return r;
}
int main(){
    int g = extgcd(30, 18);
    putint(g); putch(32);
    putint(xg); putch(32);
    putint(yg); putch(10);
    g = extgcd(240, 46);
    putint(g); putch(32);
    putint(xg); putch(32);
    putint(yg); putch(10);
    putint(modinv(3, 11)); putch(32);
    putint(modinv(10, 17)); putch(32);
    putint(modinv(7, 26)); putch(32);
    putint(modinv(6, 21)); putch(10);
    putint(modinv(2, 1000000007 % 1000)); putch(10);
    int check = 0;
    if (modinv(3, 11) * 3 % 11 == 1) check = check + 1;
    if (modinv(10, 17) * 10 % 17 == 1) check = check + 1;
    putint(check); putch(10);
    return 0;
}
