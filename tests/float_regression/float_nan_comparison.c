int main()
{
	float zero = 0.0;
	float value = zero / zero;
	int notEqual = 0;
	if (value != value) {
		notEqual = 1;
	}

	int truthy = 0;
	if (value) {
		truthy = 1;
	}

	int inverted = 0;
	if (!value) {
		inverted = 1;
	}

	putint(notEqual);
	putch(32);
	putint(truthy);
	putch(32);
	putint(inverted);
	putch(10);
	return notEqual * 4 + truthy * 2 + inverted;
}
