long twice(long x) {
return x * 2;
}

long combine(long x, long y) {
return twice(x) + twice(y);
}

long main() {
return combine(13, 8);
}
