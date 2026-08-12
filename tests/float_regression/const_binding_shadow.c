const int shared_int = 11;
const float shared_float = 2.5;

int use_params(int shared_int, float shared_float)
{
	return shared_int * 3 + shared_float;
}

int main()
{
	int result = use_params(7, 8.5);
	putint(result);
	putch(10);

	int shared_int = 29;
	float shared_float = 7.5;
	result = result + shared_int + shared_float;
	putint(result);
	putch(10);

	const int nested_int = 13;
	const float nested_float = 3.5;
	{
		int nested_int = 37;
		float nested_float = 8.5;
		result = result + nested_int + nested_float;
		putint(result);
		putch(10);
	}

	result = result + nested_int + nested_float;
	putint(result);
	putch(10);

	{
		const int shared_int = 5;
		const float shared_float = 1.5;
		result = result + shared_int + shared_float;
		putint(result);
		putch(10);
	}

	result = result + shared_int + shared_float;
	putint(result);
	putch(10);
	return result;
}
