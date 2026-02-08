#pragma once

// revng builds against system LLVM on some distros (e.g. Fedora) where a few
// helper templates used by revng to compute dominator trees on "graph views"
// have been removed from LLVM (notably in LLVM 21).
//
// Upstream revng still uses:
// - llvm::DominatorTreeOnView
// - llvm::DomTreeOnView
// - llvm::PostDomTreeOnView
//
// This header shadows <llvm/Support/GenericDomTree.h>, includes the system
// header, and then provides minimal compatible implementations of the missing
// types when building against LLVM >= 21.

#if defined(__has_include_next)
#  if __has_include_next(<llvm/Support/GenericDomTree.h>)
#    include_next <llvm/Support/GenericDomTree.h>
#  else
#    error "System LLVM does not provide <llvm/Support/GenericDomTree.h>"
#  endif
#else
#  include_next <llvm/Support/GenericDomTree.h>
#endif

#include "llvm/Config/llvm-config.h"

#if LLVM_VERSION_MAJOR >= 21

#  include "llvm/ADT/DenseMap.h"
#  include "llvm/ADT/SmallPtrSet.h"
#  include "llvm/ADT/SmallVector.h"
#  include "llvm/ADT/STLExtras.h"
#  include "llvm/Support/raw_ostream.h"

#  include <cassert>
#  include <cstddef>
#  include <cstdint>
#  include <memory>
#  include <type_traits>
#  include <vector>

namespace llvm {

// Compute a dominator/post-dominator tree on an arbitrary *view* graph.
//
// The view is expressed via GraphTraits for `ViewT<GraphT*>`. For example:
// - revng scope graph: ViewT = Scope, GraphT = llvm::Function
// - filtered RegionCFG: ViewT = EdgeFilteredGraph, GraphT = RegionCFG<...>
//
// The implementation is intentionally small and self-contained: it builds an
// immediate-dominator forest using the Cooper-Harvey-Kennedy algorithm and
// then derives `dominates()` via DFS intervals on the dominator tree.
template<typename NodeT, bool IsPostDom, template<typename> class ViewT>
class DominatorTreeOnView {
public:
  using NodeType = NodeT;
  using NodePtr = NodeT *;

private:
  struct Node final {
    NodePtr Block = nullptr;
    Node *IDom = nullptr;
    unsigned Level = 0;
    llvm::SmallVector<Node *, 4> Children;

    unsigned DFSNumIn = 0;
    unsigned DFSNumOut = 0;

    explicit Node(NodePtr B, Node *I) : Block(B), IDom(I) {
      Level = IDom ? IDom->Level + 1 : 0;
    }

    NodePtr getBlock() const { return Block; }
    Node *getIDom() const { return IDom; }
    unsigned getLevel() const { return Level; }

    using iterator = typename decltype(Children)::iterator;
    using const_iterator = typename decltype(Children)::const_iterator;

    iterator begin() { return Children.begin(); }
    iterator end() { return Children.end(); }
    const_iterator begin() const { return Children.begin(); }
    const_iterator end() const { return Children.end(); }

    iterator_range<iterator> children() {
      return llvm::make_range(begin(), end());
    }
    iterator_range<const_iterator> children() const {
      return llvm::make_range(begin(), end());
    }
  };

  // We recompute from scratch lazily after graph mutations.
  mutable bool Dirty = false;
  void *GraphStorage = nullptr;

  using RecalcThunkT = void (*)(DominatorTreeOnView *, void *);
  RecalcThunkT RecalcThunk = nullptr;

  mutable llvm::DenseMap<NodePtr, Node *> NodeMap;
  mutable std::vector<std::unique_ptr<Node>> NodesStorage;
  mutable Node *RootNode = nullptr;

private:
  template<typename GraphT>
  static void recalcThunk(DominatorTreeOnView *Self, void *Graph) {
    Self->recalculateImpl(*static_cast<GraphT *>(Graph));
  }

  void clear() const {
    NodeMap.clear();
    NodesStorage.clear();
    RootNode = nullptr;
    Dirty = false;
  }

  void ensureUpToDate() const {
    if (!Dirty)
      return;
    assert(RecalcThunk != nullptr);
    assert(GraphStorage != nullptr);
    RecalcThunk(const_cast<DominatorTreeOnView *>(this), GraphStorage);
    Dirty = false;
  }

  template<typename ViewGraphT>
  static llvm::SmallVector<NodePtr, 8> successorsDedup(NodePtr N) {
    using GT = llvm::GraphTraits<ViewGraphT>;
    llvm::SmallVector<NodePtr, 8> Out;
    llvm::SmallPtrSet<NodePtr, 8> Seen;
    for (auto It = GT::child_begin(N), End = GT::child_end(N); It != End; ++It) {
      NodePtr Succ = *It;
      if (Seen.insert(Succ).second)
        Out.push_back(Succ);
    }
    return Out;
  }

  template<typename ViewGraphT>
  void buildDomTree(ViewGraphT &View) const {
    using GT = llvm::GraphTraits<ViewGraphT>;

    const NodePtr Entry = GT::getEntryNode(View);
    assert(Entry != nullptr);

    // Compute reachable nodes in reverse-postorder (RPO).
    llvm::SmallPtrSet<NodePtr, 32> Visited;
    llvm::SmallVector<NodePtr, 128> PostOrder;

    struct Frame {
      NodePtr N;
      typename GT::ChildIteratorType It;
      typename GT::ChildIteratorType End;
    };
    llvm::SmallVector<Frame, 128> Stack;

    Visited.insert(Entry);
    Stack.push_back({ Entry, GT::child_begin(Entry), GT::child_end(Entry) });

    while (!Stack.empty()) {
      Frame &Top = Stack.back();
      if (Top.It == Top.End) {
        PostOrder.push_back(Top.N);
        Stack.pop_back();
        continue;
      }

      NodePtr Succ = *Top.It;
      ++Top.It;
      if (Visited.insert(Succ).second) {
        Stack.push_back({ Succ, GT::child_begin(Succ), GT::child_end(Succ) });
      }
    }

    llvm::SmallVector<NodePtr, 128> Nodes(PostOrder.rbegin(),
                                          PostOrder.rend());
    assert(!Nodes.empty());
    assert(Nodes.front() == Entry);

    llvm::DenseMap<NodePtr, unsigned> Index;
    Index.reserve(Nodes.size());
    for (unsigned I = 0; I < Nodes.size(); ++I)
      Index[Nodes[I]] = I;

    // Predecessor list for each node (as indices into `Nodes`).
    std::vector<llvm::SmallVector<unsigned, 4>> Preds(Nodes.size());
    for (unsigned UI = 0; UI < Nodes.size(); ++UI) {
      NodePtr U = Nodes[UI];
      for (NodePtr V : successorsDedup<ViewGraphT>(U)) {
        auto It = Index.find(V);
        if (It == Index.end())
          continue;
        Preds[It->second].push_back(UI);
      }
    }

    // Cooper-Harvey-Kennedy immediate dominators.
    std::vector<int> IDom(Nodes.size(), -1);
    IDom[0] = 0; // Entry dominates itself.

    const auto intersect = [&](int Finger1, int Finger2) -> int {
      while (Finger1 != Finger2) {
        while (Finger1 > Finger2)
          Finger1 = IDom[Finger1];
        while (Finger2 > Finger1)
          Finger2 = IDom[Finger2];
      }
      return Finger1;
    };

    bool Changed = true;
    while (Changed) {
      Changed = false;

      for (unsigned I = 1; I < Nodes.size(); ++I) {
        int NewIDom = -1;
        for (unsigned P : Preds[I]) {
          if (IDom[P] == -1)
            continue;
          if (NewIDom == -1) {
            NewIDom = static_cast<int>(P);
          } else {
            NewIDom = intersect(static_cast<int>(P), NewIDom);
          }
        }

        // A node in the reachable set should have at least one reachable pred.
        if (NewIDom == -1)
          NewIDom = 0;

        if (IDom[I] != NewIDom) {
          IDom[I] = NewIDom;
          Changed = true;
        }
      }
    }

    // Materialize nodes and parent/child edges.
    NodesStorage.reserve(Nodes.size());
    std::vector<Node *> NodeObjs(Nodes.size(), nullptr);

    for (unsigned I = 0; I < Nodes.size(); ++I) {
      NodesStorage.push_back(std::make_unique<Node>(Nodes[I], nullptr));
      NodeObjs[I] = NodesStorage.back().get();
      NodeMap[Nodes[I]] = NodeObjs[I];
    }

    RootNode = NodeObjs[0];
    for (unsigned I = 1; I < Nodes.size(); ++I) {
      const int P = IDom[I];
      Node *IDomNode = (P >= 0) ? NodeObjs[static_cast<unsigned>(P)] : nullptr;
      NodeObjs[I]->IDom = IDomNode;
      NodeObjs[I]->Level = IDomNode ? IDomNode->Level + 1 : 0;
      if (IDomNode)
        IDomNode->Children.push_back(NodeObjs[I]);
    }

    // Assign DFS in/out numbers on the dominator tree for fast dominance.
    unsigned Counter = 0;
    llvm::SmallVector<std::pair<Node *, unsigned>, 64> DFSStack;
    DFSStack.push_back({ RootNode, 0 });
    RootNode->DFSNumIn = Counter++;

    while (!DFSStack.empty()) {
      Node *N = DFSStack.back().first;
      unsigned &ChildIdx = DFSStack.back().second;
      if (ChildIdx < N->Children.size()) {
        Node *C = N->Children[ChildIdx++];
        C->DFSNumIn = Counter++;
        DFSStack.push_back({ C, 0 });
      } else {
        N->DFSNumOut = Counter++;
        DFSStack.pop_back();
      }
    }
  }

  template<typename ViewGraphT>
  void buildPostDomTree(ViewGraphT &View) const {
    using GT = llvm::GraphTraits<ViewGraphT>;

    const NodePtr Entry = GT::getEntryNode(View);
    assert(Entry != nullptr);

    // First compute nodes reachable from the entry in the *forward* view.
    llvm::SmallPtrSet<NodePtr, 32> Reachable;
    llvm::SmallVector<NodePtr, 128> Worklist;
    Reachable.insert(Entry);
    Worklist.push_back(Entry);
    while (!Worklist.empty()) {
      NodePtr N = Worklist.pop_back_val();
      for (NodePtr Succ : successorsDedup<ViewGraphT>(N)) {
        if (Reachable.insert(Succ).second)
          Worklist.push_back(Succ);
      }
    }

    // Index reachable nodes (excluding the virtual root) and build successor
    // lists for the forward view.
    llvm::SmallVector<NodePtr, 128> Nodes;
    Nodes.reserve(Reachable.size());
    for (NodePtr N : Reachable)
      Nodes.push_back(N);

    llvm::DenseMap<NodePtr, unsigned> Idx;
    Idx.reserve(Nodes.size());
    for (unsigned I = 0; I < Nodes.size(); ++I)
      Idx[Nodes[I]] = I;

    std::vector<llvm::SmallVector<unsigned, 4>> SuccFwd(Nodes.size());
    std::vector<llvm::SmallVector<unsigned, 4>> PredFwd(Nodes.size());

    for (unsigned UI = 0; UI < Nodes.size(); ++UI) {
      NodePtr U = Nodes[UI];
      for (NodePtr V : successorsDedup<ViewGraphT>(U)) {
        auto It = Idx.find(V);
        if (It == Idx.end())
          continue;
        const unsigned VI = It->second;
        SuccFwd[UI].push_back(VI);
        PredFwd[VI].push_back(UI);
      }
    }

    // Exits are nodes with no successors in the forward view.
    llvm::SmallVector<unsigned, 16> ExitNodes;
    for (unsigned I = 0; I < Nodes.size(); ++I)
      if (SuccFwd[I].empty())
        ExitNodes.push_back(I);

    // Find nodes that can reach some exit (reverse reachability from exits).
    std::vector<uint8_t> CanReachExit(Nodes.size(), 0);
    llvm::SmallVector<unsigned, 128> Stack;
    for (unsigned E : ExitNodes) {
      if (!CanReachExit[E]) {
        CanReachExit[E] = 1;
        Stack.push_back(E);
      }
    }
    while (!Stack.empty()) {
      unsigned N = Stack.pop_back_val();
      for (unsigned P : PredFwd[N]) {
        if (!CanReachExit[P]) {
          CanReachExit[P] = 1;
          Stack.push_back(P);
        }
      }
    }

    // Treat nodes that can't reach any exit as additional "exits" so the tree
    // covers the whole reachable-from-entry subgraph.
    for (unsigned I = 0; I < Nodes.size(); ++I)
      if (!CanReachExit[I])
        ExitNodes.push_back(I);

    // Build the reverse graph adjacency using indices:
    // - virtual root is index 0 (nullptr block)
    // - original nodes are shifted by +1
    const unsigned RootIdx = 0;
    const unsigned NumRevNodes = Nodes.size() + 1;
    std::vector<llvm::SmallVector<unsigned, 4>> SuccRev(NumRevNodes);
    std::vector<llvm::SmallVector<unsigned, 4>> PredRev(NumRevNodes);

    auto addRevEdge = [&](unsigned From, unsigned To) {
      SuccRev[From].push_back(To);
      PredRev[To].push_back(From);
    };

    // Root -> exits.
    {
      std::vector<uint8_t> Seen(NumRevNodes, 0);
      for (unsigned E : ExitNodes) {
        unsigned EI = E + 1;
        if (!Seen[EI]) {
          Seen[EI] = 1;
          addRevEdge(RootIdx, EI);
        }
      }
    }

    // Reverse edges: for each forward edge U->V, add V->U.
    for (unsigned U = 0; U < Nodes.size(); ++U) {
      for (unsigned V : SuccFwd[U]) {
        addRevEdge(V + 1, U + 1);
      }
    }

    // RPO of the reverse graph starting from the virtual root.
    llvm::SmallVector<unsigned, 128> RevPostOrder;
    std::vector<uint8_t> RevVisited(NumRevNodes, 0);
    struct RFrame {
      unsigned N;
      unsigned NextIdx;
    };
    llvm::SmallVector<RFrame, 128> RevStack;

    RevVisited[RootIdx] = 1;
    RevStack.push_back({ RootIdx, 0 });
    while (!RevStack.empty()) {
      RFrame &Top = RevStack.back();
      if (Top.NextIdx >= SuccRev[Top.N].size()) {
        RevPostOrder.push_back(Top.N);
        RevStack.pop_back();
        continue;
      }
      unsigned Succ = SuccRev[Top.N][Top.NextIdx++];
      if (!RevVisited[Succ]) {
        RevVisited[Succ] = 1;
        RevStack.push_back({ Succ, 0 });
      }
    }

    llvm::SmallVector<unsigned, 128> RevNodes(RevPostOrder.rbegin(),
                                              RevPostOrder.rend());
    assert(!RevNodes.empty());
    assert(RevNodes.front() == RootIdx);

    llvm::DenseMap<unsigned, unsigned> RevIndex;
    RevIndex.reserve(RevNodes.size());
    for (unsigned I = 0; I < RevNodes.size(); ++I)
      RevIndex[RevNodes[I]] = I;

    // Cooper idoms on the reverse graph.
    std::vector<int> IDom(RevNodes.size(), -1);
    IDom[0] = 0;

    const auto intersect = [&](int Finger1, int Finger2) -> int {
      while (Finger1 != Finger2) {
        while (Finger1 > Finger2)
          Finger1 = IDom[Finger1];
        while (Finger2 > Finger1)
          Finger2 = IDom[Finger2];
      }
      return Finger1;
    };

    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (unsigned I = 1; I < RevNodes.size(); ++I) {
        int NewIDom = -1;
        unsigned N = RevNodes[I];
        for (unsigned PNode : PredRev[N]) {
          auto PIt = RevIndex.find(PNode);
          if (PIt == RevIndex.end())
            continue;
          unsigned P = PIt->second;
          if (IDom[P] == -1)
            continue;
          if (NewIDom == -1)
            NewIDom = static_cast<int>(P);
          else
            NewIDom = intersect(static_cast<int>(P), NewIDom);
        }

        if (NewIDom == -1)
          NewIDom = 0;

        if (IDom[I] != NewIDom) {
          IDom[I] = NewIDom;
          Changed = true;
        }
      }
    }

    // Materialize dom tree nodes. The root has nullptr block.
    NodesStorage.reserve(RevNodes.size());
    std::vector<Node *> NodeObjs(RevNodes.size(), nullptr);

    for (unsigned I = 0; I < RevNodes.size(); ++I) {
      NodePtr Block = nullptr;
      if (RevNodes[I] != RootIdx)
        Block = Nodes[RevNodes[I] - 1];
      NodesStorage.push_back(std::make_unique<Node>(Block, nullptr));
      NodeObjs[I] = NodesStorage.back().get();
      NodeMap[Block] = NodeObjs[I];
    }

    RootNode = NodeObjs[0];
    for (unsigned I = 1; I < RevNodes.size(); ++I) {
      const int P = IDom[I];
      Node *IDomNode = (P >= 0) ? NodeObjs[static_cast<unsigned>(P)] : nullptr;
      NodeObjs[I]->IDom = IDomNode;
      NodeObjs[I]->Level = IDomNode ? IDomNode->Level + 1 : 0;
      if (IDomNode)
        IDomNode->Children.push_back(NodeObjs[I]);
    }

    unsigned Counter = 0;
    llvm::SmallVector<std::pair<Node *, unsigned>, 64> DFSStack;
    DFSStack.push_back({ RootNode, 0 });
    RootNode->DFSNumIn = Counter++;

    while (!DFSStack.empty()) {
      Node *N = DFSStack.back().first;
      unsigned &ChildIdx = DFSStack.back().second;
      if (ChildIdx < N->Children.size()) {
        Node *C = N->Children[ChildIdx++];
        C->DFSNumIn = Counter++;
        DFSStack.push_back({ C, 0 });
      } else {
        N->DFSNumOut = Counter++;
        DFSStack.pop_back();
      }
    }
  }

  template<typename GraphT>
  void recalculateImpl(GraphT &G) const {
    clear();

    using ViewGraphT = ViewT<GraphT *>;
    GraphT *GP = &G;
    ViewGraphT View(GP);

    if constexpr (IsPostDom)
      buildPostDomTree(View);
    else
      buildDomTree(View);
  }

public:
  DominatorTreeOnView() = default;
  DominatorTreeOnView(const DominatorTreeOnView &) = delete;
  DominatorTreeOnView &operator=(const DominatorTreeOnView &) = delete;
  DominatorTreeOnView(DominatorTreeOnView &&) = default;
  DominatorTreeOnView &operator=(DominatorTreeOnView &&) = default;

public:
  template<typename GraphT>
  void recalculate(GraphT &G) {
    GraphStorage = &G;
    RecalcThunk = &recalcThunk<GraphT>;
    recalculateImpl(G);
  }

  void insertEdge(NodePtr /*From*/, NodePtr /*To*/) { Dirty = true; }
  void deleteEdge(NodePtr /*From*/, NodePtr /*To*/) { Dirty = true; }

  Node *getNode(NodePtr N) const {
    ensureUpToDate();
    auto It = NodeMap.find(N);
    return It == NodeMap.end() ? nullptr : It->second;
  }

  Node *operator[](NodePtr N) const { return getNode(N); }

  Node *getRootNode() const {
    ensureUpToDate();
    return RootNode;
  }

  bool dominates(NodePtr A, NodePtr B) const {
    ensureUpToDate();
    if (A == B)
      return true;
    Node *NA = getNode(A);
    Node *NB = getNode(B);
    if (!NA || !NB)
      return false;
    return NA->DFSNumIn <= NB->DFSNumIn && NB->DFSNumOut <= NA->DFSNumOut;
  }

  void print(raw_ostream &O) const {
    ensureUpToDate();

    O << "=============================--------------------------------\n";
    if constexpr (IsPostDom)
      O << "Inorder PostDominator Tree (OnView)\n";
    else
      O << "Inorder Dominator Tree (OnView)\n";

    const Node *R = RootNode;
    if (!R) {
      O << "<empty>\n";
      return;
    }

    // Small recursive printer.
    struct Printer {
      static void printNode(const Node *N, raw_ostream &O, unsigned Lev) {
        O.indent(2 * Lev);
        if (auto *B = N->getBlock())
          B->printAsOperand(O, false);
        else
          O << " <<exit node>>";
        O << " [" << N->getLevel() << "]\n";
        for (const Node *C : N->children())
          printNode(C, O, Lev + 1);
      }
    };

    Printer::printNode(R, O, 1);
  }
};

template<typename NodeT, template<typename> class ViewT>
using DomTreeOnView = DominatorTreeOnView<NodeT, false, ViewT>;

template<typename NodeT, template<typename> class ViewT>
using PostDomTreeOnView = DominatorTreeOnView<NodeT, true, ViewT>;

} // namespace llvm

#endif // LLVM_VERSION_MAJOR >= 21
