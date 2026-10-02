// Native scalar runtime, built into a relocation-free text blob by
// scripts/generate_native_runtime.py. No assembler is invoked by polyc.
// All public functions use AAPCS64; x19-x28 and stack alignment are preserved.
.text
.p2align 2
#ifdef __APPLE__
#define SYS(n) mov x16, n; svc #0x80
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_CLOSE 6
#define SYS_MMAP 197
#define SYS_MUNMAP 73
#else
#define SYS(n) mov x8, n; svc #0
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_CLOSE 57
#define SYS_MMAP 222
#define SYS_MUNMAP 215
#endif

.globl polyrt_open_read
polyrt_open_read:
#ifdef __APPLE__
  mov x1, #0
  mov x2, #0
  SYS(5)
  b.cc 1f
#else
  mov x1, x0
  mov x0, #-100
  mov x2, #0
  mov x3, #0
  SYS(56)
  cmp x0, #0
  b.ge 1f
#endif
  mov x0, #-1
1: ret

.globl polyrt_open_write
polyrt_open_write:
#ifdef __APPLE__
  mov x1, #0x601
  mov x2, #0644
  SYS(5)
  b.cc 1f
#else
  mov x1, x0
  mov x0, #-100
  mov x2, #0x241
  mov x3, #0644
  SYS(56)
  cmp x0, #0
  b.ge 1f
#endif
  mov x0, #-1
1: ret

.globl polyrt_close_read
polyrt_close_read:
  SYS(SYS_CLOSE)
#ifdef __APPLE__
  b.cc 1f
  mov x0, #-1
#endif
1: ret

// Read a byte from fd in x19; -1 on EOF/error; retry interrupted reads.
.Lread_byte:
  sub sp, sp, #16
1: mov x0, x19
  mov x1, sp
  mov x2, #1
  SYS(SYS_READ)
#ifdef __APPLE__
  b.cc 2f
  cmp x0, #4
#else
  cmn x0, #4
#endif
  b.eq 1b
#ifdef __APPLE__
  b 3f
#endif
2: cmp x0, #1
  b.ne 3f
  ldrb w0, [sp]
  add sp, sp, #16
  ret
3: mov x0, #-1
  add sp, sp, #16
  ret

.globl polyrt_read_i64_or
polyrt_read_i64_or:
  stp x19, x20, [sp, #-48]!
  stp x21, x22, [sp, #16]
  str x30, [sp, #32]
  mov x19, x0
  mov x20, x1
.Lscan:
  mov x22, #0
  bl .Lread_byte
  cmp x0, #0
  b.lt .Lfallback
  cmp x0, #45
  b.ne .Lclassify
  mov x22, #1
  bl .Lread_byte
.Lclassify:
  sub x9, x0, #48
  cmp x9, #9
  b.hi .Lscan
  mov x21, #0
.Ldigit:
  // floor(INT64_MAX / 10), shared by positive/negative limits.
  movz x10, #0xcccc
  movk x10, #0xcccc, lsl #16
  movk x10, #0xcccc, lsl #32
  movk x10, #0x0ccc, lsl #48
  cmp x21, x10
  b.hi .Loverflow
  b.ne 1f
  add x10, x22, #7
  cmp x9, x10
  b.hi .Loverflow
1: mov x10, #10
  madd x21, x21, x10, x9
  bl .Lread_byte
  sub x9, x0, #48
  cmp x9, #9
  b.ls .Ldigit
  cmp x22, #0
  cneg x0, x21, ne
  b .Lread_done
.Loverflow:
  bl .Lread_byte
  sub x9, x0, #48
  cmp x9, #9
  b.ls .Loverflow
.Lfallback:
  mov x0, x20
.Lread_done:
  ldr x30, [sp, #32]
  ldp x21, x22, [sp, #16]
  ldp x19, x20, [sp], #48
  ret

// write(fd, bytes, length), looping on short writes and EINTR.
.Lwrite_all:
  stp x19, x20, [sp, #-32]!
  stp x21, x22, [sp, #16]
  mov x19, x0
  mov x20, x1
  mov x21, x2
  mov x22, x2
1: cbz x21, 4f
  mov x0, x19
  mov x1, x20
  mov x2, x21
  SYS(SYS_WRITE)
#ifdef __APPLE__
  b.cc 2f
  cmp x0, #4
#else
  cmn x0, #4
#endif
  b.eq 1b
#ifdef __APPLE__
  b 3f
#endif
2: cmp x0, #0
  b.le 3f
  add x20, x20, x0
  sub x21, x21, x0
  b 1b
3: mov x22, #-1
4: mov x0, x22
  ldp x21, x22, [sp, #16]
  ldp x19, x20, [sp], #32
  ret

.globl polyrt_write_text
polyrt_write_text:
  cbz x1, .Lbad_text
  mov x2, #0
1: ldrb w9, [x1, x2]
  cbz w9, 2f
  add x2, x2, #1
  b 1b
2: b .Lwrite_all
.Lbad_text:
  mov x0, #-1
  ret

.globl polyrt_write_i64
polyrt_write_i64:
  sub sp, sp, #64
  str x30, [sp, #48]
  mov x9, x1
  add x1, sp, #40
  mov x11, x1
  cbz x2, 1f
  strb w2, [x1, #-1]!
1: cmp x9, #0
  cneg x10, x9, lt
  mov x12, #10
2: udiv x13, x10, x12
  msub x14, x13, x12, x10
  add w14, w14, #48
  strb w14, [x1, #-1]!
  mov x10, x13
  cbnz x10, 2b
  cmp x9, #0
  b.ge 3f
  mov w14, #45
  strb w14, [x1, #-1]!
3: sub x2, x11, x1
  bl .Lwrite_all
  ldr x30, [sp, #48]
  add sp, sp, #64
  ret

// Integer arrays: handle points to [length, element0, ...]; mmap zeroes values.
.globl polyrt_array_new
polyrt_array_new:
  cmp x0, #0
  b.lt .Lnew_fail
  mov x9, #0x8000000
  cmp x0, x9
  b.hi .Lnew_fail
  stp x19, x30, [sp, #-16]!
  mov x19, x0
  lsl x1, x0, #3
  add x1, x1, #8
  mov x0, #0
  mov x2, #3
#ifdef __APPLE__
  mov x3, #0x1002
#else
  mov x3, #0x22
#endif
  mov x4, #-1
  mov x5, #0
  SYS(SYS_MMAP)
#ifdef __APPLE__
  b.cs 1f
#else
  cmn x0, #4095
  b.cs 1f
#endif
  str x19, [x0]
  ldp x19, x30, [sp], #16
  ret
1: ldp x19, x30, [sp], #16
.Lnew_fail:
  mov x0, #0
  ret

.globl polyrt_array_len
polyrt_array_len:
  cbz x0, 1f
  ldr x0, [x0]
1: ret

.globl polyrt_array_get
polyrt_array_get:
  cbz x0, 1f
  ldr x9, [x0]
  cmp x1, x9
  b.hs 1f
  add x0, x0, #8
  ldr x0, [x0, x1, lsl #3]
  ret
1: mov x0, x2
  ret

.globl polyrt_array_set
polyrt_array_set:
  cbz x0, 1f
  ldr x9, [x0]
  cmp x1, x9
  b.hs 1f
  add x0, x0, #8
  str x2, [x0, x1, lsl #3]
  mov x0, #0
  ret
1: mov x0, #-1
  ret

.globl polyrt_array_free
polyrt_array_free:
  cbz x0, 1f
  ldr x1, [x0]
  lsl x1, x1, #3
  add x1, x1, #8
  SYS(SYS_MUNMAP)
#ifdef __APPLE__
  b.cc 1f
  mov x0, #-1
#endif
1: ret

// args_count includes argv[0]; invalid arg_text returns a stable empty string.
.globl polyrt_arg_text
polyrt_arg_text:
  cbz x1, 1f
  cmp x2, x0
  b.hs 1f
  ldr x0, [x1, x2, lsl #3]
  ret
1: adr x0, .Lempty
  ret

.globl polyrt_arg_int
polyrt_arg_int:
  cbz x1, .Larg_fallback
  cmp x2, x0
  b.hs .Larg_fallback
  ldr x1, [x1, x2, lsl #3]
  mov x9, #0
  mov x10, #0
  ldrb w11, [x1], #1
  cmp w11, #45
  b.ne 1f
  mov x10, #1
  b 2f
1: cmp w11, #43
  b.ne .Larg_digit
2: ldrb w11, [x1], #1
.Larg_digit:
  sub x11, x11, #48
  cmp x11, #9
  b.hi .Larg_fallback
  movz x12, #0xcccc
  movk x12, #0xcccc, lsl #16
  movk x12, #0xcccc, lsl #32
  movk x12, #0x0ccc, lsl #48
  cmp x9, x12
  b.hi .Larg_fallback
  b.ne 3f
  add x12, x10, #7
  cmp x11, x12
  b.hi .Larg_fallback
3: mov x12, #10
  madd x9, x9, x12, x11
  ldrb w11, [x1], #1
  cbnz w11, .Larg_digit
  cmp x10, #0
  cneg x0, x9, ne
  ret
.Larg_fallback:
  mov x0, x3
  ret
.Lempty: .byte 0
.p2align 2
.globl polyrt_native_end
polyrt_native_end:
