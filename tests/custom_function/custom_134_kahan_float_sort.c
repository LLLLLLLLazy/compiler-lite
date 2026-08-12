/* 浮点综合: Kahan 补偿求和、浮点插入排序、浮点二分查找。
   全部使用可精确表示的二进制浮点数, 保证与 g++ -O0 参考逐位一致。
   校验浮点比较(>= <= > < ==)与排序稳定性。 */
float vals[12];
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
float kahanSum(float a[], int n) {
    float sum = 0.0;
    float comp = 0.0;
    for (int i = 0; i < n; i = i + 1) {
        float y = a[i] - comp;
        float t = sum + y;
        comp = t - sum - y;
        sum = t;
    }
    return sum;
}
int fbinSearch(float a[], int n, float key) {
    int lo = 0;
    int hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] == key) return mid;
        if (a[mid] < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}
int main(){
    int n = 8;
    vals[0] = 4.0; vals[1] = 0.5; vals[2] = 2.0; vals[3] = -1.0;
    vals[4] = 0.25; vals[5] = 8.0; vals[6] = -0.5; vals[7] = 1.0;
    putfloat(kahanSum(vals, n)); putch(10);
    fsort(vals, n);
    for (int i = 0; i < n; i = i + 1) {
        putfloat(vals[i]);
        putch(32);
    }
    putch(10);
    putint(fbinSearch(vals, n, 1.0)); putch(32);
    putint(fbinSearch(vals, n, 0.25)); putch(32);
    putint(fbinSearch(vals, n, -0.5)); putch(32);
    putint(fbinSearch(vals, n, 3.0)); putch(10);
    int cnt = 0;
    for (int i = 0; i < n; i = i + 1)
        if (vals[i] >= 0.0) cnt = cnt + 1;
    putint(cnt); putch(10);
    float mix[7];
    mix[0] = 1000.0;
    mix[1] = 0.1;
    mix[2] = 0.2;
    mix[3] = 0.3;
    mix[4] = 0.4;
    mix[5] = 0.5;
    mix[6] = 0.6;
    putfloat(kahanSum(mix, 7)); putch(10);
    float acc = 0.0;
    for (int i = 0; i < 7; i = i + 1) acc = acc + mix[i];
    putfloat(acc); putch(10);
    return 0;
}
