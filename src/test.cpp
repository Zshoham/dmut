#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <algorithm>
#include <fstream>
#include <string>
#include <sstream>
#include "dmut.h"

#define VEC_SIZE 100000000
#define ITERATIONS 10000000

void read(dmut<std::vector<int>> &data)
{
    std::mt19937 rng((std::random_device()()));
    std::uniform_int_distribution<std::mt19937::result_type> picker(0, ITERATIONS - 1);

    std::ostringstream ss;
    ss << std::this_thread::get_id();

    std::ofstream stream(ss.str(), std::ios::binary);

    auto ptr = data.peek();
    for (int i = 0; i < ITERATIONS; ++i) {
        stream.put((char) ptr->at(picker(rng)));
    }
}

void transform(dmut<std::vector<int>> &data) {
    std::mt19937 rng((std::random_device()()));
    std::uniform_int_distribution<std::mt19937::result_type> picker(0, ITERATIONS - 1);

    auto ptr = data.lock();
    for (int i = 0; i < ITERATIONS; ++i) {
        ptr->at(picker(rng)) = picker(rng);
    }
}


int main()
{
    std::mt19937 rng((std::random_device()()));

    std::uniform_int_distribution<std::mt19937::result_type> generator(INT32_MIN, INT32_MAX);

    dmut<std::vector<int>> data = make_dmut<std::vector<int>>(VEC_SIZE);

    auto ptr = data.lock();

    std::generate(ptr->begin(), ptr->end(), [&generator, &rng, &ptr] { return generator(rng); });

    ptr.unlock();

    std::thread reader1(read, std::ref(data));
    std::thread reader2(read, std::ref(data));

    std::thread transformer1(transform, std::ref(data));
    std::thread transformer2(transform, std::ref(data));

    reader1.join();
    reader2.join();
    transformer1.join();
    transformer2.join();

	return 0;
}