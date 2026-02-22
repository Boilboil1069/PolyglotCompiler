#include "runtime/include/services/threading.h"

#include <algorithm>
#include <future>
#include <numeric>
#include <unordered_set>

namespace polyglot::runtime::services {

// ---------------------- ThreadPool ----------------------
ThreadPool::ThreadPool(size_t num_threads) {
	num_threads_ = num_threads == 0 ? std::max<size_t>(1, std::thread::hardware_concurrency()) : num_threads;
	for (size_t i = 0; i < num_threads_; ++i) {
		workers_.emplace_back([this]() {
			for (;;) {
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(mutex_);
					cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
					if (stop_ && tasks_.empty()) {
						return;
					}
					task = std::move(tasks_.front());
					tasks_.pop();
					active_.fetch_add(1, std::memory_order_relaxed);
				}
				task();
				active_.fetch_sub(1, std::memory_order_relaxed);
				cv_.notify_all();
			}
		});
	}
}

ThreadPool::~ThreadPool() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	cv_.notify_all();
	for (auto &w : workers_) {
		if (w.joinable()) w.join();
	}
}

void ThreadPool::Wait() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this]() { return tasks_.empty() && active_.load(std::memory_order_relaxed) == 0; });
}

// ---------------------- TaskScheduler ----------------------
TaskScheduler::TaskScheduler() = default;

size_t TaskScheduler::Schedule(Task task) {
	task.id = next_id_++;
	tasks_.push_back(task);
	return task.id;
}

bool TaskScheduler::Cancel(size_t task_id) {
	auto it = std::remove_if(tasks_.begin(), tasks_.end(), [task_id](const Task &t) { return t.id == task_id; });
	if (it != tasks_.end()) {
		tasks_.erase(it, tasks_.end());
		return true;
	}
	return false;
}

void TaskScheduler::Execute() {
	std::unordered_set<size_t> completed;
	bool progress = true;
	while (completed.size() < tasks_.size() && progress) {
		progress = false;
		for (const auto &task : tasks_) {
			if (completed.count(task.id)) continue;
			bool ready = std::all_of(task.dependencies.begin(), task.dependencies.end(),
															 [&completed](size_t dep) { return completed.count(dep) > 0; });
			if (ready) {
				task.func();
				completed.insert(task.id);
				progress = true;
			}
		}
	}
}

// ---------------------- WorkStealingScheduler ----------------------
WorkStealingScheduler::WorkStealingScheduler(size_t num_workers) : num_workers_(num_workers) {}

// ---------------------- RWLock ----------------------
void RWLock::ReadLock() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this]() { return !writer_; });
	++readers_;
}

void RWLock::ReadUnlock() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (--readers_ == 0) cv_.notify_all();
}

void RWLock::WriteLock() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this]() { return !writer_ && readers_ == 0; });
	writer_ = true;
}

void RWLock::WriteUnlock() {
	std::lock_guard<std::mutex> lock(mutex_);
	writer_ = false;
	cv_.notify_all();
}

// ---------------------- Barrier ----------------------
Barrier::Barrier(size_t num_threads) : num_threads_(num_threads) {}

void Barrier::Wait() {
	std::unique_lock<std::mutex> lock(mutex_);
	size_t gen = generation_;
	if (++count_ == num_threads_) {
		generation_++;
		count_ = 0;
		cv_.notify_all();
	} else {
		cv_.wait(lock, [&]() { return gen != generation_; });
	}
}

void Barrier::Reset(size_t num_threads) {
	std::lock_guard<std::mutex> lock(mutex_);
	num_threads_ = num_threads;
	count_ = 0;
	generation_++;
	cv_.notify_all();
}

// ---------------------- Semaphore ----------------------
Semaphore::Semaphore(int initial_count) : count_(initial_count) {}

void Semaphore::Wait() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [&]() { return count_ > 0; });
	--count_;
}

bool Semaphore::TryWait() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (count_ > 0) {
		--count_;
		return true;
	}
	return false;
}

void Semaphore::Signal() {
	std::lock_guard<std::mutex> lock(mutex_);
	++count_;
	cv_.notify_one();
}

// ---------------------- Coroutine Scheduler ----------------------
void CoroutineScheduler::Schedule(std::shared_ptr<Coroutine> coro) {
	std::lock_guard<std::mutex> lock(mutex_);
	coroutines_.push_back(std::move(coro));
}

void CoroutineScheduler::Run() {
	std::vector<std::shared_ptr<Coroutine>> to_run;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		to_run.swap(coroutines_);
	}
	for (auto &coro : to_run) {
		coro->Resume();
	}
}

// ---------------------- ThreadProfiler ----------------------

namespace {

// Global profiling state.
static std::mutex profiler_mutex_;
static bool profiler_active_ = false;
static std::unordered_map<size_t, ThreadStats> profiler_stats_;

} // anonymous namespace

void ThreadProfiler::StartProfiling() {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	profiler_active_ = true;
}

void ThreadProfiler::StopProfiling() {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	profiler_active_ = false;
}

ThreadStats ThreadProfiler::GetStats(size_t thread_id) {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	auto it = profiler_stats_.find(thread_id);
	if (it != profiler_stats_.end()) {
		return it->second;
	}
	return ThreadStats{};
}

void ThreadProfiler::ResetStats() {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	profiler_stats_.clear();
}

// Internal function used by the runtime to record task execution metrics.
// Called from thread pool workers when profiling is active.
void RecordTaskExecution(size_t thread_id, size_t exec_time_us) {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	if (!profiler_active_) return;
	auto &stats = profiler_stats_[thread_id];
	stats.num_tasks_executed++;
	stats.total_exec_time_us += exec_time_us;
}

void RecordSteal(size_t thread_id) {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	if (!profiler_active_) return;
	profiler_stats_[thread_id].num_steals++;
}

void RecordIdleTime(size_t thread_id, size_t idle_time_us) {
	std::lock_guard<std::mutex> lock(profiler_mutex_);
	if (!profiler_active_) return;
	profiler_stats_[thread_id].idle_time_us += idle_time_us;
}

}  // namespace polyglot::runtime::services
