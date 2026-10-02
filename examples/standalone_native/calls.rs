fn twice(x: i64) -> i64 {
return x * 2;
}

fn combine(x: i64, y: i64) -> i64 {
return twice(x) + twice(y);
}

fn main() -> i64 {
return combine(13, 8);
}
