int parent[8];
int findRoot(int x) {
    while (parent[x] != x) x = parent[x];
    return x;
}
void unite(int a, int b) {
    int ra = findRoot(a);
    int rb = findRoot(b);
    if (ra != rb) parent[ra] = rb;
}
int main(){
    for (int i = 0; i < 8; i = i + 1) parent[i] = i;
    unite(0, 1);
    unite(2, 3);
    unite(1, 2);
    unite(4, 5);
    if (findRoot(0) == findRoot(3)) putint(1); else putint(0); putch(10);
    if (findRoot(0) == findRoot(4)) putint(1); else putint(0); putch(10);
    if (findRoot(5) == findRoot(4)) putint(1); else putint(0); putch(10);
    if (findRoot(6) == findRoot(6)) putint(1); else putint(0); putch(10);
    return 0;
}
