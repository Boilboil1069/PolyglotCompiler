package main
func twice(x int) int {
return x * 2;
}

func combine(x int, y int) int {
return twice(x) + twice(y);
}

func main() int {
return combine(13, 8);
}
