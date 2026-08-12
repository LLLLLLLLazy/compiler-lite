int main(){
    float a;
    float b;
    float c;
    a = 1.5;
    b = 2.25e1;
    c = 0.5;
    putfloat(a + b); putch(10);
    putfloat(a * c); putch(10);
    putfloat(1e3); putch(10);
    putfloat(0x1.8p1); putch(10);
    putfloat(-2.5); putch(10);
    putfloat(.5e1); putch(10);
    return 0;
}
