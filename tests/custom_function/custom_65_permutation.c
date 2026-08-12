int a[3];
int used[3];
void perm(int depth) {
    if (depth == 3) {
        for (int i = 0; i < 3; i = i + 1) {
            putint(a[i]);
            putch(32);
        }
        putch(10);
        return;
    }
    for (int v = 1; v <= 3; v = v + 1) {
        if (used[v - 1] == 0) {
            used[v - 1] = 1;
            a[depth] = v;
            perm(depth + 1);
            used[v - 1] = 0;
        }
    }
}
int main(){
    for (int i = 0; i < 3; i = i + 1) used[i] = 0;
    perm(0);
    return 0;
}
