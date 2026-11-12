#include <iostream>

int square(int x) {
    return x * x;
}

int main() {
    int sum = 0;
    for (int i = 1; i <= 5; ++i) {
        sum += square(i);
    }
    std::cout << "Sum of squares = " << sum << std::endl;
    return 0;
}
