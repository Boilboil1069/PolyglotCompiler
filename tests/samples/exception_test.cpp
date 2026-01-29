// Exception handling test
int safe_divide(int a, int b) {
    try {
        if (b == 0) {
            throw 42;  // Throw integer error code
        }
        return a / b;
    }
    catch (int error) {
        return -1;  // Return error indicator
    }
}

int main() {
    int result1 = safe_divide(10, 2);   // Should return 5
    int result2 = safe_divide(10, 0);   // Should return -1 (caught exception)
    return result1 + result2;  // Should return 4
}
