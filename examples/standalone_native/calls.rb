# @param x [Integer]
# @return [Integer]
def twice(x)
return x * 2
end

# @param x [Integer]
# @param y [Integer]
# @return [Integer]
def combine(x, y)
return twice(x) + twice(y)
end

# @return [Integer]
def main()
return combine(13, 8)
end
