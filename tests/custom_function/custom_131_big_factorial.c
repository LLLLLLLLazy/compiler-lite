/* 高精度阶乘: 100! 与 25! 的十进制 digit 展开, 连续乘小数的进位链。
   校验 100! 尾部零个数与总位数。 */
int digits[200];
int alen;
void fact(int n) {
    digits[0] = 1;
    alen = 1;
    for (int k = 2; k <= n; k = k + 1) {
        int carry = 0;
        for (int i = 0; i < alen; i = i + 1) {
            int t = digits[i] * k + carry;
            digits[i] = t % 10;
            carry = t / 10;
        }
        while (carry > 0) {
            digits[alen] = carry % 10;
            alen = alen + 1;
            carry = carry / 10;
        }
    }
}
void printAll() {
    for (int i = alen - 1; i >= 0; i = i - 1) putint(digits[i]);
    putch(10);
}
int main(){
    fact(100);
    putint(alen); putch(10);
    printAll();
    int zeros = 0;
    int i = 0;
    while (digits[i] == 0) {
        zeros = zeros + 1;
        i = i + 1;
    }
    putint(zeros); putch(10);
    int digitSum = 0;
    for (int k = 0; k < alen; k = k + 1) digitSum = digitSum + digits[k];
    putint(digitSum); putch(10);
    fact(25);
    printAll();
    fact(0);
    printAll();
    return 0;
}
