test: json11.cpp json11.h test.cpp
	clang++ -O -std=c++11 -stdlib=libc++ json11.cpp test.cpp -o test -fno-rtti -fno-exceptions
