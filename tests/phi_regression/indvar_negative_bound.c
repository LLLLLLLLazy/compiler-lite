int sum_prefix(int bound)
{
	int data[4] = {3, 5, 7, 11};
	int index = 0;
	int sum = 0;
	while (index < bound) {
		sum = sum + data[index];
		index = index + 1;
	}
	return sum;
}

int main()
{
	putint(sum_prefix(getint()));
	putch(32);
	putint(sum_prefix(getint()));
	putch(10);
	return 0;
}
