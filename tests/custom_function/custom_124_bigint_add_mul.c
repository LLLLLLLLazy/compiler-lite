/* 大整数(digit 数组, 十进制): 加法进位链与长乘法,
   输出 12!+13!、123456789*987654321 与 2^63 的十进制展开。
   覆盖进位传播、嵌套乘加与定长数组截断边界。 */
int digits[60];
int alen;
void setSmall(int v) {
    alen = 0;
    if (v == 0) {
        digits[0] = 0;
        alen = 1;
        return;
    }
    while (v > 0) {
        digits[alen] = v % 10;
        alen = alen + 1;
        v = v / 10;
    }
}
void mulSmall(int k) {
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
void addBig(int b[], int blen) {
    int carry = 0;
    int i = 0;
    int mx = alen;
    if (blen > mx) mx = blen;
    while (i < mx) {
        int va = 0;
        int vb = 0;
        if (i < alen) va = digits[i];
        if (i < blen) vb = b[i];
        int s = va + vb + carry;
        digits[i] = s % 10;
        carry = s / 10;
        i = i + 1;
    }
    alen = mx;
    if (carry > 0) {
        digits[alen] = carry;
        alen = alen + 1;
    }
}
void print() {
    for (int i = alen - 1; i >= 0; i = i - 1) putint(digits[i]);
    putch(10);
}
int factDigits[60];
int factLen;
int main(){
    setSmall(1);
    for (int k = 2; k <= 12; k = k + 1) mulSmall(k);
    for (int i = 0; i < alen; i = i + 1) factDigits[i] = digits[i];
    factLen = alen;
    setSmall(1);
    for (int k = 2; k <= 13; k = k + 1) mulSmall(k);
    addBig(factDigits, factLen);
    print();
    setSmall(123456789);
    mulSmall(987654321);
    print();
    setSmall(2);
    for (int k = 0; k < 62; k = k + 1) mulSmall(2);
    print();
    return 0;
}
