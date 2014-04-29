// g++ -std=c++11 -Ofast -o bench bench.cpp

#include "json11.h"
#include "json11.cpp"
#include "iostream"

namespace json = json11;
int main(int, char *[])
{
	const size_t N = 1000000;
	std::vector<json::Value> vals;
	for (int i=0; i<N; ++i) {
		vals.push_back(json::Object{{"id", i}, {"value", std::to_string(i)}});
	}

	json::Value arr(std::move(vals));
	size_t sum = 0;
	for (int i=0; i<N; ++i) {
		sum += arr[i].to_string().size();
		sum += arr[i]["value"].string_value().size();
	}

	std::cout << "total: " << sum << std::endl;
	return 0;
}

