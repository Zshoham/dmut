#include <iostream>
#include <thread>
#include <vector>
#include <array>
#include "dmut.h"

struct iwrap
{
	int val;

	explicit iwrap(const int val)
	{
		this->val = val;
		std::cout << "constructing iwrap\n";
	}

	
};

dmut<iwrap> val = new_dmut<iwrap>(5);

void print()
{
	const auto lock = val.peek();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	
	std::cout << "printing val: " << lock->val << "\n";
}

void change(int new_val)
{
	const auto lock = val.lock();

	
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	std::cout << "setting value to: " << lock->val << "\n";
}

template<typename T> std::shared_ptr<T> make_shared_array(size_t size)
{
	return std::shared_ptr<T>(new T[size], std::default_delete<T[]>());
}


int main()
{	
	dmut<int[]> mut = make_dmut<int[]>();
	
	auto lock = mut.lock();
	lock[5] = 6;
	lock.unlock();
	std::thread t2(change, 5);
	std::thread t1(print);
	
	std::this_thread::sleep_for(std::chrono::seconds(5));

	std::cout << "done waiting \n";

	
	t1.join();
	t2.join();
	
	
	return 0;
}