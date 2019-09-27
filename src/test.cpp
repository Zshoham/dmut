#include <iostream>
#include <thread>
#include "dmut.h"

dmut<int> val(22);


void print()
{
	const auto lock = val.peek();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	
	std::cout << "printing val: " << *lock << "\n";
	
}

void change(int new_val)
{
	const auto lock = val.lock();

	*lock = new_val;
	
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	std::cout << "set a new value: " << *lock << "\n";
}


int main()
{
	std::thread t1(print);
	std::thread t2(change, 5);
	
	std::this_thread::sleep_for(std::chrono::seconds(10));
	
	std::cout << "done waiting \n";
	
	t1.join();
	t2.join();
	
	
	return 0;
}