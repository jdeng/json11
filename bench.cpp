#include "json11.h"
#include "json11.cpp"
#include "iostream"

int main(int, char *[])
{
	const size_t N = 100000;
	std::vector<json11::Value> vals;
	for (int i=0; i<N; ++i) {
		vals.push_back(json11::Object{{"id", i}, {"value", std::to_string(i)}});
	}

	size_t sum = 0;
	for (int i=0; i<N; ++i) {
		sum += vals[i].to_string().size();
		sum += vals[i]["value"].string_value().size();
	}

	std::cout << "total: " << sum << std::endl;
	return 0;
}

