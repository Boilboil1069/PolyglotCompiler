// Test floating-point operations
double add_floats(double a, double b) {
    return a + b;
}

double multiply_floats(double x, double y) {
    return x * y;
}

double compute(double a, double b, double c) {
    double sum = a + b;
    double product = sum * c;
    return product / 2.0;
}

int main() {
    double x = 3.14;
    double y = 2.71;
    double result = add_floats(x, y);
    return 0;
}
