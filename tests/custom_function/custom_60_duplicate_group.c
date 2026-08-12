int main(){
    int a[8] = {1, 2, 2, 1, 1, 2, 1, 2};
    int c1 = 0;
    int c2 = 0;
    for (int i = 0; i < 8; i = i + 1) {
        if (a[i] == 1) c1 = c1 + 1;
        else c2 = c2 + 1;
    }
    putint(c1); putch(10);
    putint(c2); putch(10);
    int seen[10];
    for (int i = 0; i < 10; i = i + 1) seen[i] = 0;
    for (int i = 0; i < 8; i = i + 1) seen[a[i]] = seen[a[i]] + 1;
    for (int v = 0; v < 10; v = v + 1)
        if (seen[v] != 0) {
            putint(v);
            putf(":%d ", seen[v]);
        }
    putch(10);
    return 0;
}
