#include <vector>

namespace polyglot::backends::x86_64 {

struct LiveInterval {
  int start{0};
  int end{0};
};

std::vector<int> LinearScanAllocate(const std::vector<LiveInterval> &intervals) {
  std::vector<int> assignments(intervals.size(), 0);
  return assignments;
}

}  // namespace polyglot::backends::x86_64
