/* 长参数列表(超过寄存器传参上限, 走栈传参) + 标量/浮点/数组混合形参
   + 嵌套调用与递归调用, 压测调用约定与 caller-saved 活跃性。 */
int mix12(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) {
    return a * 2 + b * 3 + c * 4 + d * 5 + e * 6 + f * 7 + g * 8 + h * 9 + i * 10 + j * 11 + k * 12 + l * 13;
}
float fmix(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j) {
    return a * 0.5 + b * 0.25 + c * 0.125 + d * 2.0 + e * 4.0 + f * 8.0 + g * 16.0 + h * 32.0 + i * 64.0 + j * 128.0;
}
int arrSum(int a[], int n) {
    int s = 0;
    for (int i = 0; i < n; i = i + 1) s = s + a[i];
    return s;
}
int recur(int n, int acc) {
    if (n == 0) return acc;
    return recur(n - 1, acc + n);
}
int compose(int x, int y) {
    return mix12(x, y, x + y, x - y, x * y, x + 1, y + 1, x + 2, y + 2, x + 3, y + 3, x + 4) + recur(y, 0);
}
int main(){
    putint(mix12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)); putch(10);
    putint(mix12(-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12)); putch(10);
    putfloat(fmix(1.0, 2.0, 4.0, 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625, 0.0078125)); putch(10);
    int arr[10];
    for (int i = 0; i < 10; i = i + 1) arr[i] = i * i - 10;
    putint(arrSum(arr, 10)); putch(10);
    putint(compose(5, 7)); putch(10);
    putint(recur(20, 100)); putch(10);
    int s = 0;
    for (int i = 0; i < 20; i = i + 1) {
        int t = mix12(i, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
        s = s + t;
    }
    putint(s); putch(10);
    return 0;
}
