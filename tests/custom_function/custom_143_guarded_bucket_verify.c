/* 双守卫分桶校验(select 形状3: 同一循环内两个不同不变条件 bit==0 与 sat>=8)。
   条件1 控制"按位缩放 vs 原值",条件2 控制"饱和归零",%16 分桶累加。
   两个条件互不相同,应触发多轮版本化(4 份循环体)。 */
int n = 0;
int bit = 0;
int sat = 0;
int a[1000];
int pw[16];
int bucket[16];

int main()
{
	n = getarray(a);
	bit = getint();
	sat = getint();

	pw[0] = 1;
	for (int k = 1; k < 16; k = k + 1) {
		pw[k] = pw[k - 1] * 2;
	}

	for (int i = 0; i < n; i = i + 1) {
		int t = a[i] / pw[bit];
		if (bit == 0) {
			t = a[i];
		}
		if (sat >= 8) {
			t = 0;
		}
		bucket[t % 16] = bucket[t % 16] + 1;
	}

	int sum = 0;
	for (int j = 0; j < 16; j = j + 1) {
		sum = sum + bucket[j] * j;
	}
	putint(sum);
	return 0;
}
