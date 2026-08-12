float avg(float a, float b) {
    return (a + b) / 2.0;
}
float area(float r) {
    return r * r * 3.5;
}
int main(){
    putfloat(avg(1.5, 2.5)); putch(10);
    putfloat(avg(10.0, 20.0)); putch(10);
    putfloat(area(2.0)); putch(10);
    return 0;
}
