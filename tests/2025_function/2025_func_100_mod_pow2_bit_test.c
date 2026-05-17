/* 测试 2 的幂取模零值分支优化（foldPowerOfTwoRemainderZeroBranch）
 * 验证 x % ±2^k ==/!= 0 是否被正确优化为位掩码测试
 */

/* 正除数、等于零判断：x % 8 == 0 应优化为 (x & 7) == 0 */
int p_eq(int x)
{
    if (x % 8 == 0) {
        return 1;
    }
    return 0;
}

/* 正除数、不等于零判断：x % 8 != 0 应优化为 (x & 7) != 0 */
int p_ne(int x)
{
    if (x % 8 != 0) {
        return 1;
    }
    return 0;
}

/* 负除数、等于零判断：x % -8 == 0，零值判定与正除数相同 */
int n_eq(int x)
{
    if (x % -8 == 0) {
        return 1;
    }
    return 0;
}

/* 负除数、不等于零判断：x % -8 != 0 */
int n_ne(int x)
{
    if (x % -8 != 0) {
        return 1;
    }
    return 0;
}

/* 大掩码值测试：x % 4096 == 0，掩码 4095 超出 12 位立即数范围，
 * 应使用 slliw 左移指令替代 andi */
int p_eq_large(int x)
{
    if (x % 4096 == 0) {
        return 1;
    }
    return 0;
}

/* 大掩码值 + 负除数测试：x % -4096 != 0 */
int n_ne_large(int x)
{
    if (x % -4096 != 0) {
        return 1;
    }
    return 0;
}

/* 余数在分支后仍被使用的测试：优化不应生效，
 * 因为 rem 的值在 return 路径中被消费 */
int live_rem(int x)
{
    int rem = x % 8;
    if (rem == 0) {
        return rem + 10;
    }
    return rem;
}

int main()
{
    int ans = 0;
    ans = ans + p_eq(getint());
    ans = ans + p_ne(getint());
    ans = ans + n_eq(getint());
    ans = ans + n_ne(getint());
    ans = ans + p_eq_large(getint());
    ans = ans + n_ne_large(getint());
    ans = ans + live_rem(getint());
    putint(ans);
    putch(10);
    return 0;
}
