/* 组合总和(子集型, 每元素至多用一次): 跳过分支与选取分支都调用
   search(idx + 1, ...) —— idx+1 被 GVN 共享且跨两个递归调用点使用,
   正是 peephole foldUnitStepIncrements 跨 call 丢定义的回归形态。 */
int cands[8];
int n;
int cur[16];
int sols[8][16];
int solLen[8];
int solCount;
void record(int len) {
    solLen[solCount] = len;
    for (int i = 0; i < len; i = i + 1) sols[solCount][i] = cur[i];
    solCount = solCount + 1;
}
void search(int idx, int target, int len) {
    if (target == 0) {
        record(len);
        return;
    }
    if (idx >= n) return;
    if (target < 0) return;
    search(idx + 1, target, len);
    cur[len] = cands[idx];
    search(idx + 1, target - cands[idx], len + 1);
}
void run(int target) {
    solCount = 0;
    search(0, target, 0);
    putint(target); putch(58);
    putint(solCount); putch(10);
    for (int s = 0; s < solCount; s = s + 1) {
        for (int i = 0; i < solLen[s]; i = i + 1) {
            putint(sols[s][i]);
            putch(32);
        }
        putch(10);
    }
}
int main(){
    n = 6;
    cands[0] = 2; cands[1] = 3; cands[2] = 5;
    cands[3] = 6; cands[4] = 7; cands[5] = 11;
    run(13);
    run(10);
    n = 4;
    cands[0] = 1; cands[1] = 2; cands[2] = 4; cands[3] = 8;
    run(7);
    return 0;
}
