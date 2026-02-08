#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "llvm/Support/YAMLTraits.h"

#include "revng/ADT/STLExtras.h"
#include "revng/ADT/UpcastablePointer.h"
#include "revng/Support/Assert.h"

template<typename T>
struct KeyedObjectTraits;

template<typename T, typename Traits = KeyedObjectTraits<T>>
concept KeyedObjectContainerCompatible = requires(T A) {
  { Traits::key(A) };
  { Traits::fromKey(Traits::key(A)) } -> std::same_as<T>;
} && std::is_same_v<Traits, KeyedObjectTraits<T>>;

/// Inherit if T is the key of itself
template<typename T>
struct IdentityKeyedObjectTraits {
  static T key(const T &Obj) { return Obj; }

  static T fromKey(T Obj) { return Obj; }
};

/// Trivial specializations
template<std::integral T>
struct KeyedObjectTraits<T> : public IdentityKeyedObjectTraits<T> {};

template<>
struct KeyedObjectTraits<std::string>
  : public IdentityKeyedObjectTraits<std::string> {};

template<>
struct KeyedObjectTraits<llvm::StringRef>
  : public IdentityKeyedObjectTraits<llvm::StringRef> {};

static_assert(KeyedObjectContainerCompatible<int>);

template<typename T>
concept KeyedObjectContainer = requires(T &&) { T::KeyedObjectContainerTag; };

namespace revng::detail {

template<KeyedObjectContainerCompatible T>
using KOT = KeyedObjectTraits<T>;

template<KeyedObjectContainerCompatible T>
using Key = std::decay_t<decltype(KOT<T>::key(std::declval<T>()))>;

// Support for deserializing `KeyedObjectContainer`s using LLVM YAMLTraits.
// LLVM 21 requires `SequenceTraits<T>::element()`; older revng code relied on a
// now-removed "Inserter" protocol.
//
// We keep the original semantics:
// - While deserializing, we build elements in a temporary instance.
// - Elements are inserted through `batch_insert()` to preserve ordering.
// - On success we insert the final pending element; on failure we discard it.
struct YAMLSequenceStateBase {
  virtual ~YAMLSequenceStateBase() = default;
  virtual void finalize(bool Success) = 0;
};

template<typename T>
inline void *yamlSequenceTypeTag() {
  static int Tag;
  return &Tag;
}

struct YAMLSequenceStateKey {
  const void *Container = nullptr;
  const void *TypeTag = nullptr;

  bool operator==(const YAMLSequenceStateKey &) const = default;
};

struct YAMLSequenceStateKeyHash {
  size_t operator()(const YAMLSequenceStateKey &K) const noexcept {
    auto H1 = static_cast<size_t>(reinterpret_cast<std::uintptr_t>(K.Container));
    auto H2 = static_cast<size_t>(reinterpret_cast<std::uintptr_t>(K.TypeTag));
    // Basic pointer hash combine.
    return (H1 >> 4) ^ (H2 + 0x9e3779b97f4a7c15ULL + (H1 << 6) + (H1 >> 2));
  }
};

template<KeyedObjectContainer ContainerT>
struct YAMLKeyedObjectContainerState final : public YAMLSequenceStateBase {
  using value_type = typename ContainerT::value_type;
  using KOT = KeyedObjectTraits<value_type>;
  using key_type = std::decay_t<decltype(KOT::key(std::declval<value_type>()))>;

  ContainerT *Seq = nullptr;
  std::vector<std::unique_ptr<value_type>> Elements;
  unsigned NextIndex = 0;

  explicit YAMLKeyedObjectContainerState(ContainerT &Seq) : Seq(&Seq) {
  }

  value_type &element(llvm::yaml::IO &, unsigned Index) {
    revng_assert(Index == NextIndex);
    ++NextIndex;

    revng_assert(Index == Elements.size());
    Elements.emplace_back(std::make_unique<value_type>(KOT::fromKey(key_type{})));
    return *Elements.back();
  }

  void finalize(bool Success) override {
    if (!Success) {
      Elements.clear();
      return;
    }

    // Insert at the very end of YAML parsing. Keeping a long-lived batch
    // inserter (or inserting while parsing) is fragile because container
    // elements can be moved while nested sequences are still being materialized.
    auto Inserter = Seq->batch_insert();
    for (const auto &E : Elements)
      Inserter.insert(*E);
    Elements.clear();
  }
};

class YAMLSequenceStateRegistry {
public:
  template<KeyedObjectContainer ContainerT>
  YAMLKeyedObjectContainerState<ContainerT> &get(ContainerT &Seq) {
    YAMLSequenceStateKey Key{ &Seq, yamlSequenceTypeTag<ContainerT>() };
    auto It = States.find(Key);
    if (It == States.end()) {
      auto Ptr = std::make_unique<YAMLKeyedObjectContainerState<ContainerT>>(Seq);
      InsertionOrder.push_back(Ptr.get());
      It = States.emplace(Key, std::move(Ptr)).first;
    }
    return *static_cast<YAMLKeyedObjectContainerState<ContainerT> *>(It->second.get());
  }

  void finalizeAll(bool Success) {
    // Finalize in reverse creation order to avoid committing parent containers
    // (which may sort/move elements) while nested containers still keep a live
    // batch inserter.
    for (auto It = InsertionOrder.rbegin(); It != InsertionOrder.rend(); ++It)
      (*It)->finalize(Success);
    InsertionOrder.clear();
    States.clear();
  }

private:
  std::vector<YAMLSequenceStateBase *> InsertionOrder;
  std::unordered_map<YAMLSequenceStateKey,
                     std::unique_ptr<YAMLSequenceStateBase>,
                     YAMLSequenceStateKeyHash>
    States;
};

inline YAMLSequenceStateRegistry &yamlSequenceRegistry() {
  static thread_local YAMLSequenceStateRegistry Registry;
  return Registry;
}

inline void finalizeYAMLKeyedObjectContainers(bool Success) {
  yamlSequenceRegistry().finalizeAll(Success);
}

} // namespace revng::detail

template<KeyedObjectContainerCompatible T>
using DefaultKeyObjectComparator = std::less<const revng::detail::Key<T>>;

template<KeyedObjectContainer T>
struct llvm::yaml::SequenceTraits<T> {
  static size_t size(IO &TheIO, T &Seq) { return Seq.size(); }

  using container_t = std::remove_const_t<T>;
  using value_type = typename container_t::value_type;
  using reference = std::conditional_t<std::is_const_v<T>,
                                      const value_type &,
                                      value_type &>;

  static reference element(IO &TheIO, T &Seq, size_t Index) {
    revng_assert(Index < std::numeric_limits<unsigned>::max());

    if (TheIO.outputting()) {
      revng_assert(Index < Seq.size());
      auto It = Seq.begin();
      std::advance(It, Index);
      return *It;
    }

    if constexpr (std::is_const_v<T>) {
      revng_abort();
    } else {
      return revng::detail::yamlSequenceRegistry()
        .get(static_cast<container_t &>(Seq))
        .element(TheIO, static_cast<unsigned>(Index));
    }
  }
};
