#pragma once

// revng builds against system LLVM/MLIR on some distros (e.g. Fedora) where
// LLVM does not ship the Progress/Task APIs used by revng.
//
// If the system provides <llvm/Support/Progress.h>, prefer it. Otherwise,
// provide a small compatible implementation that keeps revng's progress/task
// call-sites intact.

#if defined(__has_include_next)
#  if __has_include_next(<llvm/Support/Progress.h>)
#    include_next <llvm/Support/Progress.h>
#    define REVNG_LLVM_HAS_PROGRESS 1
#  endif
#endif

#if !defined(REVNG_LLVM_HAS_PROGRESS)

#  include <mutex>
#  include <memory>
#  include <optional>
#  include <cstdint>
#  include <string>
#  include <thread>
#  include <utility>
#  include <vector>

#  include "llvm/ADT/StringRef.h"
#  include "llvm/ADT/Twine.h"

namespace llvm {

class Task;

struct TaskStack {
  std::vector<const Task *> Tasks;
};

class ProgressListener {
public:
  virtual ~ProgressListener() = default;

  virtual void handleNewTask(const Task *T) = 0;
  virtual void handleTaskCompleted(const Task *T) = 0;
  virtual void handleTaskAdvancement(const Task *T,
                                     StringRef PreviousStepName) = 0;
};

class ProgressReporter {
private:
  struct ListenerEntry {
    bool AllThreads = false;
    std::unique_ptr<ProgressListener> Listener;
  };

  std::mutex ListenersMutex;
  std::vector<ListenerEntry> Listeners;
  std::thread::id MainThread = std::this_thread::get_id();

public:
  ProgressReporter() = default;
  ProgressReporter(const ProgressReporter &) = delete;
  ProgressReporter &operator=(const ProgressReporter &) = delete;

public:
  template<typename ListenerT, typename... Args>
  void registerListener(Args &&...Arguments) {
    auto L = std::make_unique<ListenerT>(std::forward<Args>(Arguments)...);

    ListenerEntry Entry;
    if constexpr (requires { ListenerT::AllThreads; }) {
      Entry.AllThreads = ListenerT::AllThreads;
    } else {
      Entry.AllThreads = false;
    }
    Entry.Listener = std::move(L);

    std::lock_guard Lock(ListenersMutex);
    Listeners.push_back(std::move(Entry));
  }

public:
  void notifyNewTask(const Task *T);
  void notifyTaskCompleted(const Task *T);
  void notifyTaskAdvancement(const Task *T, StringRef PreviousStepName);

private:
  template<typename FnT>
  void forEachListener(FnT &&Fn) {
    const bool IsMainThread = (std::this_thread::get_id() == MainThread);
    std::lock_guard Lock(ListenersMutex);
    for (auto &Entry : Listeners) {
      if (Entry.AllThreads || IsMainThread)
        Fn(*Entry.Listener);
    }
  }
};

class Task {
private:
  unsigned Index = 0;
  std::optional<unsigned> TotalSteps;
  std::string Name;

  // -1 means "no step started yet".
  int64_t StepIndex = -1;
  std::string StepName;
  bool StepHasSingleSubtask = true;
  bool Completed = false;

public:
  Task(unsigned TotalSteps, const Twine &Name);
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  Task(Task &&) = delete;
  Task &operator=(Task &&) = delete;

  ~Task();

public:
  void advance(const Twine &NewStepName,
               bool CurrentStepHasSingleSubtask = true);

  // Some LLVM versions expose an explicit `Task::complete()` hook.
  // revng uses it to report completion before the scope ends.
  void complete();

public:
  unsigned index() const { return Index; }
  StringRef name() const { return Name; }
  int64_t stepIndex() const { return StepIndex; }
  StringRef stepName() const { return StepName; }
  std::optional<unsigned> totalSteps() const { return TotalSteps; }
  bool currentStepHasSingleSubtask() const { return StepHasSingleSubtask; }
  bool completed() const { return Completed; }

public:
  const TaskStack &stack() const;

private:
  static TaskStack &getStack();
};

// Matches revng call-sites: llvm::ProgressReport->registerListener<...>(...).
inline ProgressReporter TheProgressReporter;
inline ProgressReporter *ProgressReport = &TheProgressReporter;

inline void ProgressReporter::notifyNewTask(const Task *T) {
  forEachListener([&](ProgressListener &L) { L.handleNewTask(T); });
}

inline void ProgressReporter::notifyTaskCompleted(const Task *T) {
  forEachListener([&](ProgressListener &L) { L.handleTaskCompleted(T); });
}

inline void ProgressReporter::notifyTaskAdvancement(const Task *T,
                                                    StringRef PreviousStepName) {
  forEachListener(
    [&](ProgressListener &L) { L.handleTaskAdvancement(T, PreviousStepName); });
}

inline Task::Task(unsigned Total, const Twine &N) :
  TotalSteps(Total), Name(N.str()) {
  TaskStack &S = getStack();
  S.Tasks.push_back(this);
  Index = static_cast<unsigned>(S.Tasks.size() - 1);
  if (ProgressReport)
    ProgressReport->notifyNewTask(this);
}

inline Task::~Task() {
  if (!Completed) {
    Completed = true;
    if (ProgressReport)
      ProgressReport->notifyTaskCompleted(this);
  }

  TaskStack &S = getStack();
  if (!S.Tasks.empty() && S.Tasks.back() == this)
    S.Tasks.pop_back();
}

inline void Task::advance(const Twine &NewStepName,
                          bool CurrentStepHasSingleSubtask) {
  std::string Previous = StepName;
  ++StepIndex;
  StepName = NewStepName.str();
  StepHasSingleSubtask = CurrentStepHasSingleSubtask;

  if (ProgressReport)
    ProgressReport->notifyTaskAdvancement(this, Previous);
}

inline void Task::complete() {
  if (Completed)
    return;
  Completed = true;
  if (ProgressReport)
    ProgressReport->notifyTaskCompleted(this);
}

inline const TaskStack &Task::stack() const { return getStack(); }

inline TaskStack &Task::getStack() {
  static thread_local TaskStack Stack;
  return Stack;
}

} // namespace llvm

#endif // !REVNG_LLVM_HAS_PROGRESS
