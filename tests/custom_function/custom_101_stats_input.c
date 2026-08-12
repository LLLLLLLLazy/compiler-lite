/* 输入驱动: getarray 读入 n 个整数, 统计均值/中位数/众数/正负零计数。
   覆盖 getarray/putarray/getint, 排序 + 多遍扫描 + 输入依赖分支。 */
int data[64];
int sorted[64];
void isort(int a[], int n) {
    for (int i = 1; i < n; i = i + 1) {
        int key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
    }
}
int main(){
    int n = getarray(data);
    putarray(n, data);
    for (int i = 0; i < n; i = i + 1) sorted[i] = data[i];
    isort(sorted, n);
    int mean = 0;
    for (int i = 0; i < n; i = i + 1) mean = mean + data[i];
    mean = mean / n;
    int median = 0;
    int mid = n / 2;
    if (n % 2 == 1) median = sorted[mid];
    else median = (sorted[mid - 1] + sorted[mid]) / 2;
    int mode = sorted[0];
    int modeCnt = 0;
    int i = 0;
    while (i < n) {
        int v = sorted[i];
        int cnt = 0;
        while (i < n && sorted[i] == v) {
            cnt = cnt + 1;
            i = i + 1;
        }
        if (cnt > modeCnt) {
            modeCnt = cnt;
            mode = v;
        }
    }
    int neg = 0;
    int zero = 0;
    int pos = 0;
    for (int k = 0; k < n; k = k + 1) {
        if (data[k] < 0) neg = neg + 1;
        else {
            if (data[k] == 0) zero = zero + 1;
            else pos = pos + 1;
        }
    }
    putint(mean); putch(32);
    putint(median); putch(32);
    putint(mode); putch(32);
    putint(modeCnt); putch(10);
    putint(neg); putch(32);
    putint(zero); putch(32);
    putint(pos); putch(10);
    putarray(n, sorted);
    int q = getint();
    putint(q); putch(32);
    putint(data[q % n]); putch(10);
    return 0;
}
