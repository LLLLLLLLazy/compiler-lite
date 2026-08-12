/* 调用图与过程间优化: 未使用函数(DeadFunctionElim)、只写不读全局
   (DeadGlobalStoreElim)、可常量折叠的传参(InterproceduralConstProp)、
   小型函数内联候选、纯函数结果被复用。 */
int unused1(int x) {
    return x * x + 1;
}
int gDead = 0;
void unused2() {
    gDead = 42;
}
float unused3(float x) {
    return x * 0.5;
}
int cfold(int x) {
    return x * 3 + 1;
}
int tiny(int x) {
    return x + 1;
}
int mid(int x) {
    int a = tiny(x);
    int b = tiny(a);
    return b * 2;
}
int pureTri(int n) {
    return n * (n + 1) / 2;
}
int main(){
    putint(cfold(7)); putch(10);
    putint(cfold(-3)); putch(10);
    putint(mid(10)); putch(10);
    int s = 0;
    for (int i = 1; i <= 10; i = i + 1) {
        int t = pureTri(i);
        s = s + t;
        if (pureTri(i) != t) s = s - 100;
    }
    putint(s); putch(10);
    int a = cfold(1);
    int b = cfold(2);
    int c = cfold(3);
    putint(a + b + c); putch(10);
    return 0;
}
