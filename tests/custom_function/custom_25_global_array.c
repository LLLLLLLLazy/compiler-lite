int g[10];
void setG(int idx, int val) {
    g[idx] = val;
}
int main(){
    putint(g[0]); putch(10);
    setG(3, 42);
    setG(9, 100);
    putint(g[3]); putch(10);
    putint(g[9]); putch(10);
    putint(g[7]); putch(10);
    return 0;
}
