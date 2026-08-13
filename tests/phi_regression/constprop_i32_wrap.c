int add_wrap(int lhs, int rhs) {
	return lhs + rhs;
}

int sub_wrap(int lhs, int rhs) {
	return lhs - rhs;
}

int mul_wrap(int lhs, int rhs) {
	return lhs * rhs;
}

int return_one() {
	return 1;
}

int return_four() {
	return 4;
}

const int GLOBAL_ADD = 2147483647 + 1;
const int GLOBAL_SUB = (-2147483647 - 1) - 1;
const int GLOBAL_MUL = 1073741824 * 4;
const int GLOBAL_NEG = -(-2147483647 - 1);

int main() {
	putint(GLOBAL_ADD);
	putch(32);
	putint(GLOBAL_SUB);
	putch(32);
	putint(GLOBAL_MUL);
	putch(32);
	putint(GLOBAL_NEG);
	putch(10);
	putint(add_wrap(2147483647, 1));
	putch(32);
	putint(sub_wrap(-2147483647 - 1, 1));
	putch(32);
	putint(mul_wrap(1073741824, 4));
	putch(10);

	int i = 2147483647 + return_one();
	while (i < -2147483647) {
		i = i + 1;
	}
	putint(i);
	putch(32);

	int j = 1073741824 * return_four();
	while (j < 1) {
		j = j + 1;
	}
	putint(j);
	putch(10);
	return 0;
}
