int main(){
    int isp[101];
    for (int i = 0; i <= 100; i = i + 1) isp[i] = 1;
    isp[0] = 0;
    isp[1] = 0;
    for (int i = 2; i * i <= 100; i = i + 1) {
        if (isp[i]) {
            for (int j = i * i; j <= 100; j = j + i) isp[j] = 0;
        }
    }
    int cnt = 0;
    for (int i = 2; i <= 100; i = i + 1)
        if (isp[i]) cnt = cnt + 1;
    putint(cnt); putch(10);
    for (int i = 50; i <= 100; i = i + 1)
        if (isp[i]) {
            putint(i);
            putch(32);
        }
    putch(10);
    return 0;
}
