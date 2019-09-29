#include <iostream>
#include <thread>
#include <vector>
#include "dmut.h"

dmut<std::vector<int>> val = make_dmut<std::vector<int>>(std::vector<int>());


void print()
{
	const auto lock = val.peek();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	
	std::cout << "printing val: " << lock->front() << "\n";
}

void change(int new_val)
{
	const auto lock = val.lock();

	lock->push_back(5);
	
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	std::cout << "setting value to: " << lock->front() << "\n";
}


int main()
{
	auto lock = val.lock();
	lock->push_back(1);
	lock.unlock();
	std::thread t2(change, 5);
	std::thread t1(print);
	
	std::this_thread::sleep_for(std::chrono::seconds(5));

	std::cout << "done waiting \n";

	
	t1.join();
	t2.join();
	
	
	return 0;
}