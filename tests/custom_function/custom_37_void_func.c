int counter;
void incr(int k) {
    counter = counter + k;
}
void reset() {
    counter = 0;
    return;
}
int main(){
    incr(5);
    incr(7);
    putint(counter); putch(10);
    reset();
    putint(counter); putch(10);
    return 0;
}
