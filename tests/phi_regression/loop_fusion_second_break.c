int first_data[8];
int second_data[8];

int main() {
	int index = 0;
	while (index < 5) {
		first_data[index] = index + 1;
		index = index + 1;
	}

	index = 0;
	while (index < 5) {
		if (index == 2) {
			break;
		}
		second_data[index] = index + 10;
		index = index + 1;
	}

	int sum = 0;
	index = 0;
	while (index < 5) {
		sum = sum + first_data[index];
		index = index + 1;
	}
	putint(sum);
	putch(32);
	putint(second_data[0] + second_data[1] + second_data[2]);
	putch(10);
	return 0;
}
