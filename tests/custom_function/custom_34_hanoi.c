int moves;
void hanoi(int n, int from, int to, int via) {
    if (n == 0) return;
    hanoi(n - 1, from, via, to);
    moves = moves + 1;
    hanoi(n - 1, via, to, from);
}
int main(){
    moves = 0;
    hanoi(5, 1, 3, 2);
    putint(moves); putch(10);
    moves = 0;
    hanoi(1, 1, 3, 2);
    putint(moves); putch(10);
    moves = 0;
    hanoi(0, 1, 3, 2);
    putint(moves); putch(10);
    return 0;
}
