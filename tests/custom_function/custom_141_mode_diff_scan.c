/* 双向差分扫描 + 前缀和校验(select 形状1: 条件=参数 flag,两侧均为循环内计算)。
   热循环内默认前向差分,if(mode==0) 守卫覆盖为后向差分;条件循环不变。 */
int n = 0;
int mode = 0;
int a[1000];
int d[1000];

int main()
{
	n = getarray(a);
	mode = getint();

	int acc = 0;
	for (int i = 1; i < n; i = i + 1) {
		int v = a[i] - a[i - 1];
		if (mode == 0) {
			v = a[i - 1] - a[i];
		}
		acc = acc + v;
		d[i] = acc;
	}

	// 差分序列的前缀和校验
	int sum = 0;
	for (int j = 1; j < n; j = j + 1) {
		sum = sum + d[j];
	}
	putint(sum);
	return 0;
}
