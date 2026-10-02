/**
 * @returns {boolean}
 */
function main() {
print_i64(args_count());
print_text(arg_text(1));
print_text("\n");
print_i64(arg_int(2, -999));
print_i64(arg_int(3, -999));
print_i64(arg_int(4, -999));
let a = array_new(4);
print_i64(array_len(a));
array_set(a, 0, 42);
array_set(a, 3, -7);
print_i64(array_get(a, 0, -99));
print_i64(array_get(a, 3, -99));
print_i64(array_get(a, 4, -99));
print_i64(array_set(a, -1, 77));
print_i64(array_get(a, 0, -99));
array_free(a);
print_i64(array_new(-1));
let fd = file_open_write("native-values.txt");
file_write_text(fd, "values\n");
file_write_int(fd, 42, 10);
file_write_int(fd, -7, 10);
file_write_int(fd, arg_int(2, 0), 10);
file_close(fd);
let rd = file_open_ints("native-values.txt");
print_i64(file_next_int(rd, -999));
print_i64(file_next_int(rd, -999));
print_i64(file_next_int(rd, -999));
print_i64(file_next_int(rd, -999));
file_close(rd);
return false;
}
