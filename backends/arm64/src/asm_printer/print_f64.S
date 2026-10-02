// Inline print_f64 sequence. Input: x9 holds IEEE-754 binary64 bits.
// Clobbers only caller-saved registers. Finite values, including subnormal and
// negative zero, use exact C99 hexadecimal notation with 13 fraction digits.
// Infinity/NaN print inf/nan; NaN payloads are intentionally not textualized.
.text
sub sp,sp,#64
mov x1,sp
lsr x10,x9,#63
cbz x10,1f
mov w12,#45
strb w12,[x1],#1
1:
ubfx x10,x9,#52,#11
and x11,x9,#0x000fffffffffffff
cmp x10,#2047
b.ne 3f
cbnz x11,2f
mov w12,#105
strb w12,[x1],#1
mov w12,#110
strb w12,[x1],#1
mov w12,#102
strb w12,[x1],#1
b 12f
2:
mov w12,#110
strb w12,[x1],#1
mov w12,#97
strb w12,[x1],#1
mov w12,#110
strb w12,[x1],#1
b 12f
3:
mov w12,#48
strb w12,[x1],#1
mov w12,#120
strb w12,[x1],#1
cbz x10,4f
mov w12,#49
sub x10,x10,#1023
b 5f
4:
mov w12,#48
mov x10,#-1022
cmp x11,#0
csel x10,xzr,x10,eq
5:
strb w12,[x1],#1
mov w12,#46
strb w12,[x1],#1
mov x13,#13
6:
lsr x12,x11,#48
and x12,x12,#15
add w14,w12,#48
add w15,w12,#87
cmp w12,#10
csel w12,w14,w15,lo
strb w12,[x1],#1
lsl x11,x11,#4
subs x13,x13,#1
b.ne 6b
mov w12,#112
strb w12,[x1],#1
cmp x10,#0
mov w12,#43
mov w13,#45
csel w12,w13,w12,lt
strb w12,[x1],#1
cneg x10,x10,lt
mov x13,#1000
mov x14,#0
7:
udiv x12,x10,x13
msub x10,x12,x13,x10
cbnz x12,8f
cbnz x14,8f
cmp x13,#1
b.ne 9f
8:
add w12,w12,#48
strb w12,[x1],#1
mov x14,#1
9:
mov x15,#10
udiv x13,x13,x15
cbnz x13,7b
12:
mov w12,#10
strb w12,[x1],#1
mov x2,sp
sub x2,x1,x2
mov x1,sp
mov x0,#1
mov x16,#4
svc #0x80
add sp,sp,#64
