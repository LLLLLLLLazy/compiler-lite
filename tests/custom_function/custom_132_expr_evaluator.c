/* 表达式求值器(调度场算法): token 数组(数字/运算符/括号),
   操作符栈 + 操作数栈按优先级归约, 对多组中缀表达式求值输出。
   深层 if 链 + 栈顶扫描, 压测控制流与数组状态活跃性。 */
int toks[64];
int len;
int pos;
int tokKind[64];
int tokVal[64];
int KIND_NUM;
int KIND_PLUS;
int KIND_MINUS;
int KIND_MUL;
int KIND_DIV;
int KIND_LP;
int KIND_RP;
int opStack[32];
int opTop;
int valStack[32];
int valTop;
int prec(int op) {
    if (op == KIND_MUL || op == KIND_DIV) return 2;
    if (op == KIND_PLUS || op == KIND_MINUS) return 1;
    return 0;
}
int apply(int op) {
    int b = valStack[valTop - 1];
    int a = valStack[valTop - 2];
    valTop = valTop - 2;
    int r = 0;
    if (op == KIND_PLUS) r = a + b;
    if (op == KIND_MINUS) r = a - b;
    if (op == KIND_MUL) r = a * b;
    if (op == KIND_DIV) r = a / b;
    valStack[valTop] = r;
    valTop = valTop + 1;
    return r;
}
int eval(int src[], int n) {
    len = n;
    for (int i = 0; i < n; i = i + 1) {
        if (src[i] == -1) tokKind[i] = KIND_PLUS;
        else {
            if (src[i] == -2) tokKind[i] = KIND_MINUS;
            else {
                if (src[i] == -3) tokKind[i] = KIND_MUL;
                else {
                    if (src[i] == -4) tokKind[i] = KIND_DIV;
                    else {
                        if (src[i] == -5) tokKind[i] = KIND_LP;
                        else {
                            if (src[i] == -6) tokKind[i] = KIND_RP;
                            else {
                                tokKind[i] = KIND_NUM;
                                tokVal[i] = src[i];
                            }
                        }
                    }
                }
            }
        }
    }
    pos = 0;
    opTop = 0;
    valTop = 0;
    while (pos < len) {
        int k = tokKind[pos];
        if (k == KIND_NUM) {
            valStack[valTop] = tokVal[pos];
            valTop = valTop + 1;
        } else {
            if (k == KIND_LP) {
                opStack[opTop] = k;
                opTop = opTop + 1;
            } else {
                if (k == KIND_RP) {
                    while (opTop > 0 && opStack[opTop - 1] != KIND_LP) {
                        opTop = opTop - 1;
                        apply(opStack[opTop]);
                    }
                    opTop = opTop - 1;
                } else {
                    while (opTop > 0 && opStack[opTop - 1] != KIND_LP && prec(opStack[opTop - 1]) >= prec(k)) {
                        opTop = opTop - 1;
                        apply(opStack[opTop]);
                    }
                    opStack[opTop] = k;
                    opTop = opTop + 1;
                }
            }
        }
        pos = pos + 1;
    }
    while (opTop > 0) {
        opTop = opTop - 1;
        apply(opStack[opTop]);
    }
    return valStack[0];
}
int main(){
    KIND_NUM = 0; KIND_PLUS = 1; KIND_MINUS = 2;
    KIND_MUL = 3; KIND_DIV = 4; KIND_LP = 5; KIND_RP = 6;
    int e1[5];
    e1[0] = 2; e1[1] = -3; e1[2] = 3; e1[3] = -1; e1[4] = 4;
    putint(eval(e1, 5)); putch(10);
    int e2[9];
    e2[0] = -5; e2[1] = 1; e2[2] = -1; e2[3] = 2; e2[4] = -4;
    e2[5] = 10; e2[6] = -2; e2[7] = 2; e2[8] = -6;
    putint(eval(e2, 9)); putch(10);
    int e3[7];
    e3[0] = 3; e3[1] = -1; e3[2] = 2; e3[3] = -3; e3[4] = 2; e3[5] = -2;
    e3[6] = 7;
    putint(eval(e3, 7)); putch(10);
    int e4[9];
    e4[0] = -5; e4[1] = 2; e4[2] = -2; e4[3] = 3; e4[4] = -6;
    e4[5] = -3; e4[6] = 2; e4[7] = -2; e4[8] = 3;
    putint(eval(e4, 9)); putch(10);
    int e5[13];
    e5[0] = -5; e5[1] = 1; e5[2] = -1; e5[3] = 2; e5[4] = -6;
    e5[5] = -3; e5[6] = -5; e5[7] = 4; e5[8] = -2; e5[9] = 1; e5[10] = -6;
    e5[11] = -4; e5[12] = 3;
    putint(eval(e5, 13)); putch(10);
    int e6[11];
    e6[0] = -5; e6[1] = 2; e6[2] = -1; e6[3] = 3; e6[4] = -6;
    e6[5] = -3; e6[6] = 4; e6[7] = -2; e6[8] = -5; e6[9] = 8; e6[10] = -6;
    putint(eval(e6, 11)); putch(10);
    return 0;
}
