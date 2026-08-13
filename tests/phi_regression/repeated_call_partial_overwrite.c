void mutate(int data[])
{
	data[0] = data[0] + 1;
	data[1] = data[1] + 1;
}

int main()
{
	int data[2] = {0, 0};
	data[0] = getint();
	data[1] = getint();
	int index = 0;
	while (index < 3) {
		int old = data[1];
		data[0] = old;
		mutate(data);
		index = index + 1;
	}
	putint(data[0]);
	putch(32);
	putint(data[1]);
	putch(10);
	return 0;
}
