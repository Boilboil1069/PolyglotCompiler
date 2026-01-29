// Simple C++ test program for end-to-end compilation
int add(int a, int b) {
    return a + b;
}

int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    int x = 5;
    int y = 3;
    int sum = add(x, y);
    int fact = factorial(5);
    return sum + fact;
}
