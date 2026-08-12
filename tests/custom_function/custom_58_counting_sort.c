int main(){
    int a[12] = {2, 0, 1, 2, 1, 0, 2, 1, 0, 2, 1, 0};
    int cnt[3];
    cnt[0] = 0;
    cnt[1] = 0;
    cnt[2] = 0;
    for (int i = 0; i < 12; i = i + 1) cnt[a[i]] = cnt[a[i]] + 1;
    for (int v = 0; v < 3; v = v + 1)
        for (int i = 0; i < cnt[v]; i = i + 1) {
            putint(v);
            putch(32);
        }
    putch(10);
    return 0;
}
