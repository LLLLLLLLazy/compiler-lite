int a[3];
int sum = 0;
void subset(int idx) {
    if (idx == 3) {
        putint(sum);
        putch(32);
        return;
    }
    subset(idx + 1);
    sum = sum + a[idx];
    subset(idx + 1);
    sum = sum - a[idx];
}
int main(){
    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    subset(0);
    putch(10);
    return 0;
}
