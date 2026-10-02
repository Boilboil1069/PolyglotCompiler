class Program {
static int twice(int x) {
return x * 2;
}

static int combine(int x, int y) {
return twice(x) + twice(y);
}

static int main() {
return combine(13, 8);
}
}
