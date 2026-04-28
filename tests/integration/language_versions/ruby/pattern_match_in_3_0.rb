# Fixture: Ruby source that requires Ruby 3.0 (pattern matching `case ... in`).
# PolyglotCompiler's Ruby frontend propagates the requested Ruby version
# through the .ploy `LANG` pragma so the downstream MRI runtime selects a
# matching interpreter.

def describe(value)
  case value
  in [Integer => x, Integer => y]
    "pair: #{x},#{y}"
  in {name: String => n}
    "named: #{n}"
  else
    "other"
  end
end
