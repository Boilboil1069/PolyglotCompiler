# strutil — a tiny vendored gem fixture used by RbImportResolver tests.

module Strutil
  def self.shout(s)
    s.upcase + "!"
  end

  def self.whisper(s)
    s.downcase
  end
end
