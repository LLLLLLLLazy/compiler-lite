int seed;
int nextRand() {
    seed = seed * 1103515245 + 12345;
    if (seed < 0) seed = seed + 2147483647 + 1;
    int r = seed % 100;
    if (r < 0) r = r + 100;
    return r;
}
int main(){
    seed = 1;
    for (int i = 0; i < 10; i = i + 1) {
        putint(nextRand());
        putch(32);
    }
    putch(10);
    return 0;
}
