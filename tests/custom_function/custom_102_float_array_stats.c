/* 输入驱动浮点: getfarray 读入 float 数组, 求均值/总体方差/最值/排序后输出。
   全部使用可精确表示的二进制浮点数(二进有理数), 避免 fmadd 融合舍入差异。
   覆盖 getfarray/putfarray/putfloat, 浮点比较与浮点数组循环。 */
float vals[16];
float sortedv[16];
void fsort(float a[], int n) {
    for (int i = 1; i < n; i = i + 1) {
        float key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
    }
}
int main(){
    int n = getfarray(vals);
    putfarray(n, vals);
    float sum = 0.0;
    float mn = vals[0];
    float mx = vals[0];
    for (int i = 0; i < n; i = i + 1) {
        sum = sum + vals[i];
        if (vals[i] < mn) mn = vals[i];
        if (vals[i] > mx) mx = vals[i];
    }
    float mean = sum / n;
    float var = 0.0;
    for (int i = 0; i < n; i = i + 1) {
        float d = vals[i] - mean;
        var = var + d * d;
    }
    var = var / n;
    putfloat(mean); putch(32);
    putfloat(var); putch(10);
    putfloat(mn); putch(32);
    putfloat(mx); putch(10);
    for (int i = 0; i < n; i = i + 1) sortedv[i] = vals[i];
    fsort(sortedv, n);
    putfarray(n, sortedv);
    float med = (sortedv[(n - 1) / 2] + sortedv[n / 2]) * 0.5;
    putfloat(med); putch(10);
    return 0;
}
