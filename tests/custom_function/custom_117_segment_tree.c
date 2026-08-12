/* 线段树: 递归建树 + 区间和查询 + 单点更新。
   递归树结构、区间分割与更新后回写, 压测递归参数传递与数组索引。 */
int tree[64];
int base[16];
void build(int node, int l, int r) {
    if (l == r) {
        tree[node] = base[l];
        return;
    }
    int mid = (l + r) / 2;
    build(node * 2, l, mid);
    build(node * 2 + 1, mid + 1, r);
    tree[node] = tree[node * 2] + tree[node * 2 + 1];
}
int query(int node, int l, int r, int ql, int qr) {
    if (ql <= l && r <= qr) return tree[node];
    if (qr < l || r < ql) return 0;
    int mid = (l + r) / 2;
    return query(node * 2, l, mid, ql, qr) + query(node * 2 + 1, mid + 1, r, ql, qr);
}
void update(int node, int l, int r, int pos, int val) {
    if (l == r) {
        tree[node] = val;
        return;
    }
    int mid = (l + r) / 2;
    if (pos <= mid) update(node * 2, l, mid, pos, val);
    else update(node * 2 + 1, mid + 1, r, pos, val);
    tree[node] = tree[node * 2] + tree[node * 2 + 1];
}
int main(){
    int n = 8;
    for (int i = 0; i < n; i = i + 1) base[i] = i * 3 - 7;
    build(1, 0, n - 1);
    putint(query(1, 0, n - 1, 0, 7)); putch(32);
    putint(query(1, 0, n - 1, 2, 5)); putch(32);
    putint(query(1, 0, n - 1, 7, 7)); putch(32);
    putint(query(1, 0, n - 1, 3, 3)); putch(10);
    update(1, 0, n - 1, 4, 100);
    update(1, 0, n - 1, 0, -20);
    putint(query(1, 0, n - 1, 0, 7)); putch(32);
    putint(query(1, 0, n - 1, 4, 4)); putch(32);
    putint(query(1, 0, n - 1, 3, 6)); putch(10);
    return 0;
}
