/* 确定性随机化: LCG 驱动 Fisher-Yates 洗牌 + 随机轴快排 + 蒙特卡洛 π。
   校验洗牌是排列、排序正确、重复运行序列一致。 */
int seed;
int nextRand() {
    seed = seed * 1103515245 + 12345;
    if (seed < 0) seed = seed + 2147483647 + 1;
    int r = seed % 1000000;
    if (r < 0) r = r + 1000000;
    return r;
}
int arr[16];
void shuffle(int n) {
    for (int i = n - 1; i >= 1; i = i - 1) {
        int j = nextRand() % (i + 1);
        int t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}
void qsortRand(int lo, int hi) {
    if (lo >= hi) return;
    int pivot = arr[lo + nextRand() % (hi - lo + 1)];
    int i = lo;
    int j = hi;
    while (i <= j) {
        while (arr[i] < pivot) i = i + 1;
        while (arr[j] > pivot) j = j - 1;
        if (i <= j) {
            int t = arr[i];
            arr[i] = arr[j];
            arr[j] = t;
            i = i + 1;
            j = j - 1;
        }
    }
    qsortRand(lo, j);
    qsortRand(i, hi);
}
int main(){
    seed = 20260813;
    for (int i = 0; i < 16; i = i + 1) arr[i] = i * 7 % 16 - 5;
    shuffle(16);
    int sum = 0;
    int seen = 0;
    for (int i = 0; i < 16; i = i + 1) {
        sum = sum + arr[i];
        seen = seen + arr[i] * arr[i];
    }
    putint(sum); putch(32);
    putint(seen); putch(10);
    qsortRand(0, 15);
    int sortedOk = 1;
    for (int i = 1; i < 16; i = i + 1)
        if (arr[i - 1] > arr[i]) sortedOk = 0;
    putint(sortedOk); putch(10);
    for (int i = 0; i < 16; i = i + 1) {
        putint(arr[i]);
        putch(32);
    }
    putch(10);
    int inside = 0;
    for (int i = 0; i < 20000; i = i + 1) {
        int x = nextRand() % 1000 - 500;
        int y = nextRand() % 1000 - 500;
        if (x * x + y * y <= 250000) inside = inside + 1;
    }
    putint(inside); putch(10);
    return 0;
}
