# Iris model format v1

`iris_model.pmodel` is newline-separated signed decimal integers. There are no
numeric comments or labels because `file_next_int` intentionally scans every
integer in a text stream.

| Position | Field |
|---:|---|
| 0 | magic `1230129491` (`IRIS`) |
| 1 | schema version `1` |
| 2 | data scale `10` |
| 3 | weight scale `100` |
| 4 | epochs `40` |
| 5 | batch size `12` |
| 6 | parameter count `18` |
| 7–10 | regression weights 0–2 and bias |
| 11–12 | decision-tree petal-length and petal-width thresholds |
| 13–24 | four feature sums for class 0, then class 1, then class 2 |
| 25 | rolling checksum |

The checksum covers only the 18 parameters, in serialized order:

```text
hash = 0
for value in parameters:
    hash = (hash * 131 + value + 100000) mod 2147483647
```

For the committed data and protocol the checksum is `858372164`. Reload is
accepted only if metadata, every trained value, checksum, and EOF all match.
