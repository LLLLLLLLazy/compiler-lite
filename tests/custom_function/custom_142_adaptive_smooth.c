/* 自适应窗口平滑校验和(select 形状2: 条件=不变 icmp(参数>常量),两侧为不同访存组合)。
   默认三点平滑,if(wnd<=8) 守卫覆盖为两点差分;条件只依赖参数,循环不变。 */
int n = 0;
int wnd = 0;
int a[1000];

int main()
{
	n = getarray(a);
	wnd = getint();

	int acc = 0;
	for (int i = 1; i < n - 1; i = i + 1) {
		int v = a[i - 1] + a[i] + a[i + 1];
		if (wnd <= 8) {
			v = a[i - 1] + a[i + 1];
		}
		acc = acc + v;
		acc = acc % 1000003;
	}
	putint(acc);
	return 0;
}
