/**
 * @file     hot_reload.cpp
 * @brief    Implementation of `hot_reload.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/runtime/hot_reload.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui::runtime {
namespace {

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

void HotReloadEngine::RegisterHandler(std::string language,
                                      ReloadHandler handler) {
  handlers_[std::move(language)] = std::move(handler);
}

std::string HotReloadEngine::DetectLanguage(const std::string &file) {
  auto dot = file.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = ToLower(file.substr(dot + 1));
  if (ext == "ploy") return "ploy";
  if (ext == "py")   return "python";
  if (ext == "cc" || ext == "cpp" || ext == "cxx" || ext == "h" ||
      ext == "hpp" || ext == "hxx")
    return "cpp";
  if (ext == "rs")   return "rust";
  if (ext == "java") return "java";
  if (ext == "cs")   return "dotnet";
  return "";
}

ReloadResult HotReloadEngine::Notify(const ReloadRequest &request) {
  // Coalesce: if a reload for the same file is already running, queue.
  auto in_flight_it = in_flight_.find(request.file);
  if (in_flight_it != in_flight_.end() && in_flight_it->second) {
    pending_[request.file] = request;
    ReloadResult queued;
    queued.status  = ReloadStatus::kPartial;
    queued.message = "queued: reload already in flight";
    return queued;
  }

  ReloadHandler handler;
  auto it = handlers_.find(request.language);
  if (it != handlers_.end()) {
    handler = it->second;
  } else {
    auto fallback = handlers_.find("");
    if (fallback != handlers_.end()) handler = fallback->second;
  }
  if (!handler) {
    ReloadResult r;
    r.status  = ReloadStatus::kUnsupported;
    r.message = "no handler registered for language: " + request.language;
    return r;
  }

  in_flight_[request.file] = true;
  ReloadResult result = handler(request);
  in_flight_[request.file] = false;
  return result;
}

bool HotReloadEngine::DrainPending(const std::string &file) {
  auto it = pending_.find(file);
  if (it == pending_.end()) return false;
  ReloadRequest req = it->second;
  pending_.erase(it);
  Notify(req);
  return true;
}

}  // namespace polyglot::tools::ui::runtime
