int sq(int x) {
    return x * x;
}
int cube(int x) {
    return x * x * x;
}
int main(){
    putint(sq(sq(3))); putch(10);
    putint(sq(2 + 3)); putch(10);
    putint(cube(2) + sq(3)); putch(10);
    putint(sq(cube(2))); putch(10);
    return 0;
}
