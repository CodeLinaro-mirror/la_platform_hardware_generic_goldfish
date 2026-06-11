#include <iostream>

void functionC(int x) {
    std::cout << "In function C: " << x << std::endl;
    volatile int* p = nullptr;
    *p = x;
}

void functionB(int x) {
    std::cout << "In function B, calling C" << std::endl;
    functionC(x + 10);
}

void functionA(int x) {
    std::cout << "In function A, calling B" << std::endl;
    functionB(x + 5);
}

int main() {
    std::cout << "Starting playground..." << std::endl;
    functionA(42);
    return 0;
}
