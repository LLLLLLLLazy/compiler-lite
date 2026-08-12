int main(){
    float a = 0.1;
    float b = 0.2;
    float c = a + b;
    putfloat(c); putch(10);
    float s = 0.0;
    int i = 0;
    while (i < 10) {
        s = s + 0.1;
        i = i + 1;
    }
    putfloat(s); putch(10);
    putfloat(a * 3.0); putch(10);
    putfloat(b / 0.25); putch(10);
    return 0;
}
