int main(){
    int n = 7;
    int k = 3;
    int alive[10];
    for (int i = 0; i < n; i = i + 1) alive[i] = 1;
    int remaining = n;
    int idx = 0;
    while (remaining > 1) {
        int step = 0;
        while (step < k) {
            if (alive[idx]) step = step + 1;
            if (step < k) idx = (idx + 1) % n;
        }
        alive[idx] = 0;
        remaining = remaining - 1;
        idx = (idx + 1) % n;
    }
    for (int i = 0; i < n; i = i + 1)
        if (alive[i]) putint(i);
    putch(10);
    return 0;
}
