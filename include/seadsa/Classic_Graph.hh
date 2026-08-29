/**
 * @file Graph.hh
 * @brief Core data structures for Sea-DSA (Data Structure Analysis)
 *
 * This file defines the main components of the Sea-DSA graph representation:
 * - Graph: Container for DSA nodes and mappings from LLVM values to memory
 * cells
 * - Node: Represents a memory object with fields, links, and type information
 * - Cell: A reference to a specific offset within a memory object
 * - Field: Represents a typed offset within a memory object
 *
 * Sea-DSA performs context-sensitive, field-sensitive pointer analysis to
 * partition memory into equivalence classes (nodes) where each node represents
 * a set of memory locations that may alias.
 */

#pragma once

#include "boost/container/flat_map.hpp"
#include "boost/container/flat_set.hpp"
#include "boost/functional/hash.hpp"
#include "boost/iterator/filter_iterator.hpp"
#include "boost/iterator/indirect_iterator.hpp"
#include "boost/optional/optional.hpp"

// llvm 3.8: forward declarations not enough
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/MapVector.h"

#include "seadsa/AllocSite.hh"
#include "seadsa/FieldType.hh"

#include <functional>

namespace llvm {
class Type;
class DataLayout;
class raw_ostream;
} // namespace llvm

namespace seadsa {

class Node;
class Cell;
class Graph;
class SimulationMapper;
using CellRef = std::unique_ptr<Cell>;

class DsaCallSite;
class DsaAllocator;

/// Global flag controlling whether DSA uses type-aware field sensitivity
extern bool g_IsTypeAware;
/// Global flag controlling whether DSA uses partial offset collapse
extern bool g_IsPartialCollapseEnabled;

/// Data structure graph traversal iterator
template <typename T> class NodeIterator;

/**
 * @brief Custom deleter for DSA nodes that uses a pooled allocator
 *
 * This deleter works with std::unique_ptr to properly deallocate nodes
 * using the custom DsaAllocator rather than standard delete.
 */
struct DsaAllocatorDeleter {
  DsaAllocator *m_allocator;
  DsaAllocatorDeleter(DsaAllocator &allocator) : m_allocator(&allocator) {}
  DsaAllocatorDeleter(const DsaAllocatorDeleter &o) = default;
  DsaAllocatorDeleter &operator=(const DsaAllocatorDeleter &o) = default;

  /// Deallocates a block using the custom allocator
  void operator()(void *block);
};

/**
 * @class Graph
 * @brief Represents a DSA graph containing memory objects (nodes) and their
 * relationships
 * @details A DSA graph per procedure,
 * it tracks necessary info that current procedure manipulates.
 * A Graph maintains:
 * - A collection of DSA nodes representing memory objects
 * - Mappings from LLVM values to cells (offsets into nodes)
 * - Allocation site information
 * - Call site information for interprocedural analysis
 *
 * The graph supports both context-sensitive (one graph per function) and
 * context-insensitive analysis modes. Graphs can be unified through the
 * import operation during bottom-up or top-down propagation.
 */
class Graph {
  friend class Node;

public:
  using Set = llvm::ImmutableSet<llvm::Type *>;
  using SetFactory = typename Set::Factory;

protected:
  /// @brief LLVM data layout for size and offset calculations
  const llvm::DataLayout &m_dl;

  /// @brief Factory for creating immutable type sets
  SetFactory &m_setFactory;

  std::unique_ptr<DsaAllocator> m_allocator;
  /// DSA nodes owned by this graph
  using NodeVector = std::vector<std::unique_ptr<Node, DsaAllocatorDeleter>>;
  using NodeVectorElemTy = std::unique_ptr<Node, DsaAllocatorDeleter>;
  /// @brief Collection of nodes in the graph
  NodeVector m_nodes;

  // NOTE: these maps are iterated by the bottom-up/top-down phases (e.g.
  // Graph::globals()) and the order of the resulting clone/unify calls
  // decides which node becomes the representative in Node::unifyAt. A
  // pointer-hashed DenseMap makes that order depend on heap addresses (ASLR)
  // and the analysis result nondeterministic. MapVector iterates in
  // insertion order, which is fixed by the IR.
  using ValueMap = llvm::MapVector<const llvm::Value *, CellRef>;
  /// @brief Map from scalars to cells in this graph
  ValueMap m_values;

  using ArgumentMap = llvm::MapVector<const llvm::Argument *, CellRef>;
  /// @brief Map from formal arguments to cells
  ArgumentMap m_formals;

  using ReturnMap = llvm::MapVector<const llvm::Function *, CellRef>;
  /// @brief Map from formal returns of functions to cells
  ReturnMap m_returns;

  using AllocSites = std::vector<std::unique_ptr<DsaAllocSite>>;
  /// @brief Allocation sites referred by this graph
  AllocSites m_allocSites;

  using ValueToAllocSite = llvm::DenseMap<const llvm::Value *, DsaAllocSite *>;
  /// @brief Map from LLVM instructions to their allocation sites
  ValueToAllocSite m_valueToAllocSite;

  using CallSites =
      std::vector<std::unique_ptr<DsaCallSite>>; /// Indirect call sites owned
                                                 /// by this graph
  ///
  /// The call site can be defined in the current function or any
  /// direct or indirect callee. Call sites are copied from callees to
  /// callers during bottom-up propagation.
  CallSites m_callSites;

  using InstructionToCallSite =
      llvm::DenseMap<const llvm::Instruction *, DsaCallSite *>;
  /// @brief Map from instructions to call sites
  InstructionToCallSite m_instructionToCallSite;

  //  Whether the graph is flat or not
  bool m_is_flat;

  /// @brief Get the set factory for creating immutable type sets
  SetFactory &getSetFactory() { return m_setFactory; }

  /// @brief Create an empty immutable set of types
  Set emptySet() { return m_setFactory.getEmptySet(); }

  /// @brief Return a new set that is the union of old and a set containing v
  /// @param old Existing immutable set
  /// @param v Type to add to the set
  /// @return New immutable set containing all types from old plus v
  Set mkSet(Set old, const llvm::Type *v) { return m_setFactory.add(old, v); }

  /// @brief Get the data layout used for size/offset calculations
  const llvm::DataLayout &getDataLayout() const { return m_dl; }

  /// @brief Predicate functor for filtering global values
  struct IsGlobal {
    bool operator()(const ValueMap::value_type &kv) const;
  };

public:
  using const_iterator =
      boost::indirect_iterator<typename NodeVector::const_iterator>;
  using iterator = boost::indirect_iterator<typename NodeVector::iterator>;
  using scalar_const_iterator = ValueMap::const_iterator;
  using global_const_iterator =
      boost::filter_iterator<IsGlobal, typename ValueMap::const_iterator>;
  using formal_const_iterator = ArgumentMap::const_iterator;
  using return_const_iterator = ReturnMap::const_iterator;
  using alloc_site_iterator =
      boost::indirect_iterator<typename AllocSites::iterator>;
  using alloc_site_const_iterator =
      boost::indirect_iterator<typename AllocSites::const_iterator>;
  using callsite_iterator =
      boost::indirect_iterator<typename CallSites::iterator>;
  using callsite_const_iterator =
      boost::indirect_iterator<typename CallSites::const_iterator>;

  /**
   * @brief Construct a new Graph object
   * @param dl LLVM data layout for size/offset calculations
   * @param sf Factory for creating immutable type sets
   * @param is_flat If true, creates a flat graph with a single node
   */
  Graph(const llvm::DataLayout &dl, SetFactory &sf, bool is_flat = false);

  virtual ~Graph();

  /**
   * @brief Remove all forwarding nodes by following forwarding chains
   *
   * After nodes are unified, some nodes become forwarding pointers to other
   * nodes. This method compresses the graph by eliminating these intermediate
   * forwarding nodes.
   *
   * @example
   * Before compression:
   *    Node A --8 bytes--> Node B --8 bytes--> Node C
   *    if a link from A to some other node D exists.
   * After compression:
   *   Node A --16 bytes--> Node C
   *   and the link from A to D becomes a link from C to D.
   *   Remove node A and B from the graph.
   */
  virtual void compress();

  /**
   * @brief Remove all dead (unreachable) nodes from the graph
   *
   * Dead nodes are those not reachable from any scalar value, formal parameter,
   * or return value.
   */
  virtual void remove_dead();

  /**
   * @brief Allocate a new DSA node in this graph
   * @return Reference to the newly created node
   * @attention Node without initialization, no info is set
   */
  virtual Node &mkNode();

  /**
   * @brief Clone an existing node
   * @param n The node to clone
   * @param cpAllocSites If true, copy allocation site information
   * @return Reference to the cloned node
   */
  virtual Node &cloneNode(const Node &n, bool cpAllocSites = true);

  /// @brief Get iterator to first node (const version)
  virtual const_iterator begin() const;
  /// @brief Get iterator past last node (const version)
  virtual const_iterator end() const;
  /// @brief Get iterator to first node
  virtual iterator begin();
  /// @brief Get iterator past last node
  virtual iterator end();

  /// @brief Get the total number of nodes in the graph
  size_t numNodes() const { return m_nodes.size(); };

  /// @brief Get the number of collapsed (field-insensitive) nodes
  size_t numCollapsed() const;

  /// @brief Get iterator to first scalar-to-cell mapping
  virtual scalar_const_iterator scalar_begin() const;
  /// @brief Get iterator past last scalar-to-cell mapping
  virtual scalar_const_iterator scalar_end() const;
  /// @brief Get range over all scalar-to-cell mappings
  llvm::iterator_range<scalar_const_iterator> scalars() const {
    return llvm::make_range(scalar_begin(), scalar_end());
  }

  /// @brief Get iterator to first global variable mapping
  virtual global_const_iterator globals_begin() const;
  /// @brief Get iterator past last global variable mapping
  virtual global_const_iterator globals_end() const;
  /// @brief Get range over all global variable mappings
  llvm::iterator_range<global_const_iterator> globals() const {
    return llvm::make_range(globals_begin(), globals_end());
  }

  /// @brief Get iterator to first formal parameter mapping
  virtual formal_const_iterator formal_begin() const;
  /// @brief Get iterator past last formal parameter mapping
  virtual formal_const_iterator formal_end() const;
  /// @brief Get range over all formal parameter mappings
  llvm::iterator_range<formal_const_iterator> formals() const {
    return llvm::make_range(formal_begin(), formal_end());
  }

  /// @brief Get iterator to first return value mapping
  virtual return_const_iterator return_begin() const;
  /// @brief Get iterator past last return value mapping
  virtual return_const_iterator return_end() const;
  /// @brief Get range over all return value mappings
  llvm::iterator_range<return_const_iterator> returns() const {
    return llvm::make_range(return_begin(), return_end());
  }

  /**
   * @brief Create or update a cell for the given value
   * @param v The LLVM value
   * @param c The cell to associate with the value
   * @return Reference to the cell (may unify with existing cell)
   */
  virtual Cell &mkCell(const llvm::Value &v, const Cell &c);

  /**
   * @brief Create or update a return cell for the given function
   * @param fn The function
   * @param c The cell to associate with the return value
   * @return Reference to the return cell
   */
  virtual Cell &mkRetCell(const llvm::Function &fn, const Cell &c);

  /**
   * @brief Get the cell associated with a value
   * @param v The LLVM value
   * @return The cell for this value
   */
  virtual const Cell &getCell(const llvm::Value &v);

  /**
   * @brief Check if a value has an associated cell
   * @param v The LLVM value
   * @return true if the value has a cell, false otherwise
   */
  virtual bool hasCell(const llvm::Value &v) const;

  virtual bool hasScalarCell(const llvm::Value &v) {
    return m_values.count(&v) > 0;
  }

  virtual bool hasRetCell(const llvm::Function &fn) const {
    return m_returns.count(&fn) > 0;
  }

  virtual Cell &getRetCell(const llvm::Function &fn);

  virtual const Cell &getRetCell(const llvm::Function &fn) const;

  llvm::Optional<DsaAllocSite *> getAllocSite(const llvm::Value &v) const {
    auto it = m_valueToAllocSite.find(&v);
    if (it != m_valueToAllocSite.end()) return it->second;

    return llvm::None;
  }

  DsaAllocSite *mkAllocSite(const llvm::Value &v);

  void clearCallSites();

  llvm::iterator_range<alloc_site_iterator> alloc_sites() {
    alloc_site_iterator begin = m_allocSites.begin();
    alloc_site_iterator end = m_allocSites.end();
    return llvm::make_range(begin, end);
  }

  llvm::iterator_range<alloc_site_const_iterator> alloc_sites() const {
    alloc_site_const_iterator begin = m_allocSites.begin();
    alloc_site_const_iterator end = m_allocSites.end();
    return llvm::make_range(begin, end);
  }

  bool hasAllocSiteForValue(const llvm::Value &v) const {
    return m_valueToAllocSite.count(&v) > 0;
  }

  // return null if no callsite found
  DsaCallSite *getCallSite(const llvm::Instruction &cs) {
    auto it = m_instructionToCallSite.find(&cs);
    if (it != m_instructionToCallSite.end()) {
      return &*it->second;
    } else {
      return nullptr;
    }
  }

  DsaCallSite *mkCallSite(const llvm::Instruction &cs, Cell c);

  llvm::iterator_range<callsite_iterator> callsites();

  llvm::iterator_range<callsite_const_iterator> callsites() const;

  /**
   * @brief Compute a simulation relation map from callee nodes to caller nodes
   *
   * Used during interprocedural analysis to map memory objects from the callee
   * context to the caller context at a specific call site.
   *
   * @param cs The call site connecting caller and callee
   * @param calleeG The callee's DSA graph
   * @param callerG The caller's DSA graph
   * @param simMap Output: mapping from callee nodes to caller nodes
   * @param reportIfSanityCheckFailed If true, report when sanity checks fail
   * @return true if mapping was successfully computed, false otherwise
   */
  static bool
  computeCalleeCallerMapping(const DsaCallSite &cs, Graph &calleeG,
                             Graph &callerG, SimulationMapper &simMap,
                             const bool reportIfSanityCheckFailed = true);

  /**
   * @brief Compute a simulation relation between two arbitrary graphs
   *
   * Determines if one graph can be "simulated" by another, meaning all
   * memory accesses in fromG have corresponding accesses in toG.
   *
   * @param fromG The source graph
   * @param toG The target graph
   * @param simMap Output: mapping from source nodes to target nodes
   * @param onlyModified If true, only consider modified nodes
   * @return true if fromG is simulated by toG, false otherwise
   */
  static bool computeSimulationMapping(Graph &fromG, Graph &toG,
                                       SimulationMapper &simMap,
                                       bool onlyModified = false);

  /**
   * @brief Import the given graph into the current one
   *
   * Copies all nodes from g and unifies all common scalars. Used during
   * bottom-up or top-down propagation to merge callee information into caller.
   *
   * @param g The graph to import
   * @param withFormals If true, also import formal parameter mappings
   */
  virtual void import(const Graph &g, bool withFormals = false);

  /**
   * @brief Pretty-print the graph to an output stream
   * @param o Output stream
   */
  virtual void write(llvm::raw_ostream &o) const;

  /// @brief Dump graph to stderr (for use in debugger)
  void dump() const;

  friend void WriteDsaGraph(Graph &g, const std::string &filename);
  /// write the Dsa graph in dot format
  void writeGraph(const std::string &filename);

  friend void ShowDsaGraph(Graph &g);
  /// view the Dsa graph using GraphViz. (For debugging.)
  void viewGraph();

  bool isFlat() const { return m_is_flat; }

  void removeLinks(Node *n, std::function<bool(const Node *)> pred);

  void removeNodes(std::function<bool(const Node *)> pred);
};

/**
 * @class FlatGraph
 * @brief A graph with a single collapsed node (field-insensitive)
 *
 * FlatGraph represents the most conservative DSA analysis where all
 * memory is collapsed into a single node. This loses all field sensitivity
 * but is sound and fast. Useful for baseline comparisons or when precision
 * is not required.
 */
class FlatGraph : public Graph {

public:
  /**
   * @brief Construct a flat graph
   * @param dl Data layout
   * @param sf Set factory
   */
  FlatGraph(const llvm::DataLayout &dl, SetFactory &sf) : Graph(dl, sf, true) {}

  /**
   * @brief Always returns the same single node
   * @return Reference to the singleton node
   */
  virtual Node &mkNode() override;
};

/**
 * @class Field
 * @brief Represents a typed offset within a memory object
 *
 * A Field combines an offset (in bytes) with type information to provide
 * field-sensitive analysis. When type-aware mode is enabled, fields with
 * different types at the same offset are distinguished.
 */
class Field {
  unsigned m_offset = -1; ///< Byte offset into the memory object
  FieldType m_type =
      FIELD_TYPE_NOT_IMPLEMENTED; ///< Type information for this field

  constexpr std::tuple<unsigned, FieldType> asTuple() const {
    return {m_offset, m_type};
  };

public:
  Field() = default;

  /**
   * @brief Construct a field with offset and type
   * @param offset Byte offset
   * @param type Type information
   */
  Field(unsigned offset, FieldType type) : m_offset(offset), m_type(type) {}
  Field(const Field &) = default;
  Field &operator=(const Field &) = default;

  /**
   * @brief Create a new field by adding to the offset
   * @param offset Offset to add
   * @return New field with adjusted offset
   */
  Field addOffset(unsigned offset) const { return {m_offset + offset, m_type}; }

  /**
   * @brief Create a new field by subtracting from the offset
   * @param offset Offset to subtract
   * @return New field with adjusted offset
   */
  Field subOffset(unsigned offset) const { return {m_offset - offset, m_type}; }

  /// @brief Get the byte offset of this field
  unsigned getOffset() const { return m_offset; }

  /// @brief Get the type information for this field
  FieldType getType() const { return m_type; }

  /// @brief Check if this field has an omni (wildcard) type
  bool hasOmniType() const { return m_type.isOmniType(); }

  Field mkOmniField() const { return Field(m_offset, FieldType::mkOmniType()); }

  bool operator<(const Field &o) const { return asTuple() < o.asTuple(); }
  bool operator==(const Field &o) const { return asTuple() == o.asTuple(); }

  void dump(llvm::raw_ostream &os = llvm::errs()) const {
    os << "<" << m_offset << ", ";
    m_type.dump(os);
    os << ">";
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &o, const Field &f) {
    f.dump(o);
    return o;
  }
};

/**
 * @class Cell
 * @brief A memory cell representing an offset into a DSA node
 *
 * A Cell is essentially a pair (Node*, offset) that references a specific
 * field or location within a memory object. Cells are the fundamental unit
 * for tracking data flow and aliasing in DSA.
 *
 * Cells can be:
 * - Null (pointing to no node)
 * - Direct (pointing to a non-forwarding node)
 * - Indirect (pointing to a forwarding node, resolved on access)
 */
class Cell {
protected:
  mutable Node *m_node =
      nullptr; ///< The memory object (node) this cell refers to
  mutable unsigned m_offset = 0; ///< Byte offset within the node

  constexpr std::tuple<Node *, unsigned> asTuple() const {
    return std::make_tuple(m_node, m_offset);
  };

public:
  /// @brief Default constructor creates a null cell
  Cell() = default;
  Cell(const Cell &) = default;

  /**
   * @brief Construct a cell pointing to a node at given offset
   * @param node Pointer to the node
   * @param offset Byte offset into the node
   */
  Cell(Node *node, unsigned offset) : m_node(node), m_offset(offset) {}

  /**
   * @brief Construct a cell pointing to a node at given offset
   * @param node Reference to the node
   * @param offset Byte offset into the node
   */
  Cell(Node &node, unsigned offset) : m_node(&node), m_offset(offset) {}

  /**
   * @brief Construct a cell from another cell with additional offset
   * @param o Base cell
   * @param offset Additional offset to add
   */
  Cell(const Cell &o, unsigned offset)
      : m_node(o.m_node), m_offset(o.m_offset + offset) {}

  Cell &operator=(const Cell &o) = default;

  ~Cell() = default;

  bool operator==(const Cell &o) const { return asTuple() == o.asTuple(); }
  bool operator!=(const Cell &o) const { return !operator==(o); }
  bool operator<(const Cell &o) const { return asTuple() < o.asTuple(); }

  /// @brief Mark this cell's node as read
  void setRead(bool v = true);

  /// @brief Mark this cell's node as modified (written to)
  void setModified(bool v = true);

  /// @brief Check if this cell's node has been read
  bool isRead() const;

  /// @brief Check if this cell's node has been modified
  bool isModified() const;

  /// @brief Check if this is a null cell (not pointing to any node)
  bool isNull() const { return m_node == nullptr; }

  /**
   * @brief Get the node this cell points to
   * @return Pointer to the node (follows forwarding chains)
   */
  Node *getNode() const;

  /**
   * @brief Get the raw offset (for internal DSA use)
   * @return The unadjusted offset
   */
  unsigned getRawOffset() const;

  /**
   * @brief Get the adjusted offset (for DSA clients)
   * @return Offset adjusted based on node properties (collapse, array, etc.)
   */
  unsigned getOffset() const;

  /**
   * @brief Make this cell point to a node at given offset
   * @param n The target node
   * @param offset Byte offset into the target node
   */
  void pointTo(Node &n, unsigned offset);

  /**
   * @brief Make this cell point to the same location as another cell
   * @param c The target cell
   * @param offset Additional offset to add
   */
  void pointTo(const Cell &c, unsigned offset = 0) {
    assert(!c.isNull());
    Node *n = c.getNode();
    pointTo(*n, c.getRawOffset() + offset);
  }

  inline bool hasLink(Field offset) const;
  inline const Cell &getLink(Field offset) const;
  inline void setLink(Field offset, const Cell &c);
  inline void addLink(Field offset, const Cell &c);
  inline void addAccessedType(unsigned offset, llvm::Type *t);
  inline void growSize(unsigned offset, llvm::Type *t);

  /**
   * @brief Unify this cell with another cell
   *
   * After unification, both cells point to the same offset of the same node.
   * This operation may cause collapse of the nodes if they cannot be unified
   * in a field-sensitive manner.
   *
   * @param c The cell to unify with
   */
  void unify(Cell &c);

  void swap(Cell &o) {
    std::swap(m_node, o.m_node);
    std::swap(m_offset, o.m_offset);
  }

  /// pretty-printer of a cell
  void write(llvm::raw_ostream &o) const;

  /// for gdb
  void dump() const;
};

/**
 * @class Chunk
 * @brief A special class represent collapsed fields
 *
 * A chunk represents a set of fields that are overlapped.
 * The overlapped means pointers refer fields that in the chunk space are may
 * alias. A chunk is essentially a pair (Node*, start, end) that references a
 * specific chunk of fields within a memory object. Chunk may grow when
 * overlapped range expands. Each chunk maintains:
 * - Node* m_node: The memory object (node) this chunk belongs to
 * - start offset: the start offset that indicate the following fields are
 * collapsed
 * - end offset: the end offset that indicate the previous fields are collapsed
 *
 * Chunk can be:
 * - Null (pointing to no node)
 * - Direct (pointing to a non-forwarding node)
 * - Indirect (pointing to a forwarding node, resolved on access)
 */
class Chunk {
protected:
  mutable Node *m_node =
      nullptr; ///< The memory object (node) this cell refers to
  mutable unsigned m_start =
      0; ///< Byte offset within the node, start of the chunk
  mutable boost::optional<unsigned> m_end =
      boost::none; ///< Byte offset within the node, end of the chunk, default
                   ///< is infty

public:
  /// @brief Default constructor creates a null chunk
  Chunk() = default;
  Chunk(const Chunk &) = default;

  /**
   * @brief Construct a chunk pointing to a node at given offsets
   * @param node Pointer to the node
   * @param start Start byte offset into the node
   * @param end End byte offset into the node
   */
  Chunk(Node *node, unsigned start, unsigned end)
      : m_node(node), m_start(start), m_end(end) {}

  /**
   * @brief Construct a chunk pointing to a node at given offsets
   * @param node Reference to the node
   * @param start Start byte offset into the node
   * @param end End byte offset into the node
   */
  Chunk(Node &node, unsigned start, unsigned end)
      : m_node(&node), m_start(start), m_end(end) {}

  Chunk(Node &node, unsigned start, boost::optional<unsigned> end)
      : m_node(&node), m_start(start), m_end(end) {}

  Chunk(Node *node, unsigned start, boost::optional<unsigned> end)
      : m_node(node), m_start(start), m_end(end) {}

  /**
   * @brief Construct a chunk from another chunk with additional offset
   * @param o Base chunk
   * @param offset Additional offset to add
   */
  Chunk(const Chunk &o, unsigned offset)
      : m_node(o.m_node), m_start(o.m_start + offset) {
    if (o.m_end) { m_end = o.m_end.get() + offset; }
  }

  Chunk &operator=(const Chunk &o) = default;

  bool operator==(const Chunk &o) const {
    return o.m_node == m_node && o.m_start == m_start && o.m_end == m_end;
  }
  bool operator!=(const Chunk &o) const { return !operator==(o); }
  bool operator<(const Chunk &o) const {
    if (m_node != o.m_node) return false;
    if (m_start != o.m_start)
      return m_start < o.m_start;
    else if (m_end && o.m_end) {
      return m_end.get() < o.m_end.get();
    } else if (m_end && !o.m_end) {
      return false; // this is finite, other is infinite
    } else if (!m_end && o.m_end) {
      return true; // this is infinite, other is finite
    } else {
      return false; // both infinite
    }
  }

  /// @brief Check if this is a null chunk (not pointing to any node)
  bool isNull() const { return m_node == nullptr; }

  bool includes(const Chunk &o) const {
    return this->operator==(o) || this->operator<(o);
  }

  bool includes(const unsigned o) const {
    return (m_start <= o && (m_end ? o <= m_end.get() : true));
  }

  bool isDisjoint(const Chunk &o) const {
    if (m_node != o.m_node) return true;
    if (!m_end && !o.m_end)
      return false; // Both infinite ranges are not disjoint
    if (!m_end) return o.m_end && o.m_end.get() < m_start; // This is infinite
    if (!o.m_end) return m_end.get() < o.m_start;          // Other is infinite
    return (m_end.get() < o.m_start || o.m_end.get() < m_start);
  }

  void mergeChunkInPlace(const Chunk &o) {
    assert(m_node == o.m_node);
    if (o.m_start < m_start) m_start = o.m_start;
    if (!m_end) {
      m_end = o.m_end;
    } else if (o.m_end && o.m_end.get() > m_end.get()) {
      m_end = o.m_end;
    } else if (!o.m_end) {
      m_end = boost::none; // Merge with infinite makes infinite
    }
  }

  // Chunk mergeChunk(const Chunk &o) const {
  //   assert(m_node == o.m_node);
  //   unsigned new_start = m_start < o.m_start ? m_start : o.m_start;
  //   unsigned new_end;
  //   if (!m_end && !o.m_end) {
  //     // Both infinite - return a chunk with infinite end
  //     Chunk result(m_node, new_start, 0);
  //     result.m_end = boost::none;
  //     return result;
  //   } else if (!m_end) {
  //     Chunk result(m_node, new_start, 0);
  //     result.m_end = boost::none;
  //     return result;
  //   } else if (!o.m_end) {
  //     Chunk result(m_node, new_start, 0);
  //     result.m_end = boost::none;
  //     return result;
  //   } else {
  //     new_end = m_end.get() > o.m_end.get() ? m_end.get() : o.m_end.get();
  //     return Chunk(m_node, new_start, new_end);
  //   }
  // }

  unsigned getRawStartOffset() const;

  unsigned getStartOffset() const;

  /**
   * @brief Get the raw offset (for internal DSA use)
   * @return The unadjusted offset
   */
  boost::optional<unsigned> getRawEndOffset() const;

  /**
   * @brief Get the adjusted offset (for DSA clients)
   * @return Offset adjusted based on node properties (collapse, array, etc.)
   */
  boost::optional<unsigned> getEndOffset() const;

  /**
   * @brief Get the node this cell points to
   * @return Pointer to the node (follows forwarding chains)
   */
  Node *getNode() const;

  /**
   * @brief Make this chunk point to a node at given offset
   * @param n The target node
   * @param offset Byte offset into the target node
   */
  void pointTo(Node &n, unsigned start, unsigned end);

  void swap(Chunk &c) {
    std::swap(m_node, c.m_node);
    std::swap(m_start, c.m_start);
    std::swap(m_end, c.m_end);
  }

  /// pretty-printer of a cell
  void write(llvm::raw_ostream &o) const;

  void dump() const;

  inline void addLink(Field offset, const Cell &c);
};

/**
 * @class Node
 * @brief A DSA node representing a memory object
 *
 * A Node represents an equivalence class of memory locations that may alias.
 * Each node maintains:
 * - Type information for each field/offset (accessed_types)
 * - Points-to information for each field/offset (links)
 * - Allocation site information
 * - Various flags indicating properties (heap, stack, global, etc.)
 *
 * Nodes can be:
 * - Field-sensitive: Different offsets tracked separately
 * - Field-insensitive (collapsed): All offsets treated uniformly
 * - Arrays: Offsets computed modulo array size
 * - Forwarding: Pointing to another node after unification
 */
class Node {
  friend class Graph;
  friend class FlatGraph;
  friend class Cell;

  friend class FunctionalMapper;
  friend class SimulationMapper;

public:
  /**
   * @struct NodeType
   * @brief Bit flags representing various properties of a DSA node
   */
  struct NodeType {
    unsigned shadow : 1;           ///< Shadow node (for context sensitivity)
    unsigned foreign : 1;          ///< Foreign node (for internal use)
    unsigned alloca : 1;           ///< Stack-allocated object
    unsigned heap : 1;             ///< Heap-allocated object
    unsigned global : 1;           ///< Global variable
    unsigned externFunc : 1;       ///< External function pointer
    unsigned externGlobal : 1;     ///< External global variable
    unsigned unknown : 1;          ///< Unknown allocation type
    unsigned incomplete : 1;       ///< Incomplete type information
    unsigned modified : 1;         ///< Node has been written to
    unsigned read : 1;             ///< Node has been read from
    unsigned array : 1;            ///< Node represents an array
    unsigned offset_collapsed : 1; ///< All offsets collapsed to one
    unsigned type_collapsed : 1;   ///< All types collapsed
    unsigned external : 1;         ///< Externally visible
    unsigned inttoptr : 1;         ///< Result of inttoptr cast
    unsigned ptrtoint : 1;         ///< Result of ptrtoint cast
    unsigned vastart : 1;          ///< va_start object
    unsigned dead : 1;             ///< Dead/unreachable node
    unsigned null : 1;             ///< Null pointer allocation

    /// @brief Default constructor initializes all flags to false
    NodeType() { reset(); }

    /**
     * @brief Join (union) this node type with another
     * @param n The node type to join with
     */
    void join(const NodeType &n) {
      shadow |= n.shadow;
      foreign &= n.foreign;
      alloca |= n.alloca;
      heap |= n.heap;
      global |= n.global;
      externFunc |= n.externFunc;
      externGlobal |= n.externGlobal;
      unknown |= n.unknown;
      incomplete |= n.incomplete;
      modified |= n.modified;
      read |= n.read;
      array |= n.array;
      offset_collapsed |= n.offset_collapsed;
      type_collapsed |= n.type_collapsed;
      external |= n.external;
      inttoptr |= n.inttoptr;
      ptrtoint |= n.ptrtoint;
      vastart |= n.vastart;
      dead |= n.dead;
      null |= n.null;

      // XXX: cannot be offset-collapsed and array at the same time
      if (offset_collapsed && array) array = 0;
    }
    /// @brief Reset all flags to false
    void reset() { memset(this, 0, sizeof(*this)); }

    /**
     * @brief Convert flags to string representation
     * @return String with single-character codes for set flags
     */
    std::string toStr() const {
      std::string flags;

      if (offset_collapsed) flags += "oC";
      if (type_collapsed) flags += "tC";
      if (alloca) flags += "S";
      if (heap) flags += "H";
      if (global) flags += "G";
      if (array) flags += "A";
      if (unknown) flags += "U";
      if (incomplete) flags += "I";
      if (modified) flags += "M";
      if (read) flags += "R";
      if (external) flags += "E";
      if (externFunc) flags += "X";
      if (externGlobal) flags += "Y";
      if (inttoptr) flags += "P";
      if (ptrtoint) flags += "2";
      if (vastart) flags += "V";
      if (dead) flags += "D";
      if (null) flags += "N";
      return flags;
    }
  };

private:
  Graph *m_graph;             ///< Parent DSA graph containing this node
  struct NodeType m_nodeType; ///< Bit flags for node properties

  mutable const llvm::Value
      *m_unique_scalar;          ///< Unique scalar if node has exactly one
  bool m_has_once_unique_scalar; ///< True if had a unique scalar at some point

  /**
   * @brief Forwarding destination cell
   *
   * When nodes are unified, the smaller node becomes a forwarding pointer
   * to the larger node. This cell specifies where this node forwards to.
   */
  Cell m_forward;

public:
  using Set = Graph::Set;

  /// Map from byte offset to set of types accessed at that offset
  // TODO: Investigate why flat_map is slower for accessed_types_type.
  using accessed_types_type = llvm::DenseMap<unsigned, Set>;
  /// Map from field to the cell it points to (points-to graph edges)
  using links_type = boost::container::flat_map<Field, CellRef>;
  /// Set of Chunks (Intervals)
  using chunks_type = boost::container::flat_set<Chunk>;

  /// Iterator for graph interface (defined in GraphTraits.h)
  using iterator = NodeIterator<Node>;
  using const_iterator = NodeIterator<const Node>;

  /// @brief Begin iterator for nodes reachable from this node
  iterator begin();
  /// @brief End iterator for nodes reachable from this node
  iterator end();

  /// @brief Begin iterator for nodes reachable from this node (const)
  const_iterator begin() const;
  /// @brief End iterator for nodes reachable from this node (const)
  const_iterator end() const;

  NodeType getNodeType() const { return m_nodeType; }

  const links_type &getLinks() const { return m_links; }

  const chunks_type &getChunks() const { return m_chunks; }
  const chunks_type &getCollapsedCells() const { return getChunks(); }

private:
  links_type &getLinks() { return m_links; }

protected:
  class Offset;
  friend class Offset;

  /**
   * @class Offset
   * @brief Helper class to ensure offsets are properly adjusted
   *
   * This class wraps offset calculations to handle:
   * - Offset collapse: all offsets map to 0
   * - Array indexing: offsets computed modulo array size
   * - Normal offsets: passed through unchanged
   */
  class Offset {
    const Node &m_node;
    const unsigned m_offset;

  public:
    /**
     * @brief Construct an Offset wrapper
     * @param n The node
     * @param offset The raw offset
     */
    Offset(const Node &n, unsigned offset) : m_node(n), m_offset(offset) {}

    /**
     * @brief Get the adjusted numeric offset
     * @return Offset adjusted for collapse/array properties
     */
    unsigned getNumericOffset() const;

    /**
     * @brief Get a field with adjusted offset
     * @param f The field
     * @return Field with offset adjusted for node properties
     */
    Field getAdjustedField(Field f) {
      return Field(getNumericOffset(), f.getType());
    }

    /**
     * @brief Static helper to get adjusted field
     * @param n The node
     * @param f The field
     * @return Field with adjusted offset
     */
    static Field getAdjustedField(const Node &n, Field f) {
      return Offset(n, f.getOffset()).getAdjustedField(f);
    }

    const Node &node() const { return m_node; }
  };

private:
  accessed_types_type
      m_accessedTypes;  ///< Map from offset to types accessed at that offset
  links_type m_links;   ///< Map from field to cells pointed to by that field
  chunks_type m_chunks; ///< Set of collapsed chunks in this node

  unsigned m_size; ///< Size of the memory object in bytes

  /// Set of allocation sites (instructions) that create this object
  using AllocaSet = boost::container::flat_set<const llvm::Value *>;
  AllocaSet m_alloca_sites;

  /// @todo This is ugly. IDs should probably be unique per-graph, not unique
  /// overall.
  static uint64_t m_id_factory; ///< Global factory for assigning unique IDs

  uint64_t m_id; ///< Globally unique ID for this node

  /**
   * @brief Construct a new node in the given graph
   * @param g The parent graph
   */
  Node(Graph &g);

  /**
   * @brief Copy constructor (for cloning)
   * @param g The parent graph for the new node
   * @param n The node to copy from
   * @param cpLinks If true, copy links (points-to edges)
   * @param cpAllocSites If true, copy allocation site information
   */
  Node(Graph &g, const Node &n, bool cpLinks = false, bool cpAllocSites = true);

  void compress() {
    constexpr unsigned shrinkThreshold = 4;
    if (m_accessedTypes.size() * shrinkThreshold <
        m_accessedTypes.getMemorySize())
      m_accessedTypes =
          accessed_types_type(m_accessedTypes.begin(), m_accessedTypes.end());

    if (m_links.size() * shrinkThreshold < m_links.capacity())
      m_links.shrink_to_fit();

    if (m_alloca_sites.size() * shrinkThreshold < m_alloca_sites.capacity())
      m_alloca_sites.shrink_to_fit();
  }

  /**
   * @brief Transfer links/types to another node and make this node forward to
   * it
   *
   * Transfers all information from this node to the target node at the
   * specified offset, then makes this node a forwarding pointer. May cause
   * collapse if unification cannot preserve field sensitivity.
   *
   * @note Most clients should use unifyAt() which has less stringent
   * preconditions
   * @param node The target node
   * @param offset Where in the target node to transfer information
   */
  void pointTo(Node &node, const Offset &offset);

  void joinChunks(const Node &node, const Offset &offset);

  Cell &getLink_(const Field &_f);

  /// Adds a set of types for a field at a given offset
  void addAccessedType(const Offset &offset, Set types);

  /// joins all the types of a given node starting at a given
  /// offset of the current node
  void joinAccessedTypes(unsigned offset, const Node &n);

  /// increase size to accommodate a field of type t at the given offset
  void growSize(const Offset &offset, const llvm::Type *t);
  Node &setArray(bool v = true) {
    m_nodeType.array = v;
    return *this;
  }

  void writeAccessedTypes(llvm::raw_ostream &o) const;

public:
  /// Delete copy constructor (nodes cannot be copied directly)
  Node(const Node &n) = delete;
  /// Delete assignment (nodes cannot be assigned)
  Node &operator=(const Node &n) = delete;

  /**
   * @brief Unify this node with another at offset 0
   * @param n The node to unify with
   */
  void unify(Node &n) { unifyAt(n, 0); }

  /**
   * @brief Unify a node with a specified offset of this node
   *
   * Post-condition: The given node points to this node at the specified offset.
   * May cause collapse if field-sensitive unification is not possible.
   *
   * @param n The node to unify
   * @param offset The offset in this node where n should unify
   */
  void unifyAt(Node &n, unsigned offset);
  void unifyAt(Node &n, unsigned o1, unsigned o2);

  /**
   * @brief Mark this node as stack-allocated (alloca)
   * @param v If true, mark as alloca
   * @return Reference to this node (for chaining)
   */
  Node &setAlloca(bool v = true) {
    m_nodeType.alloca = v;
    return *this;
  }

  /**
   * @brief Mark this node as heap-allocated
   * @param v If true, mark as heap
   * @return Reference to this node (for chaining)
   */
  Node &setHeap(bool v = true) {
    m_nodeType.heap = v;
    return *this;
  }

  /**
   * @brief Mark this node as read from
   * @param v If true, mark as read
   * @return Reference to this node (for chaining)
   */
  Node &setRead(bool v = true) {
    m_nodeType.read = v;
    return *this;
  }

  /**
   * @brief Mark this node as modified (written to)
   * @param v If true, mark as modified
   * @return Reference to this node (for chaining)
   */
  Node &setModified(bool v = true) {
    m_nodeType.modified = v;
    return *this;
  }

  /**
   * @brief Mark this node as externally visible
   * @param v If true, mark as external
   * @return Reference to this node (for chaining)
   */
  Node &setExternal(bool v = true) {
    m_nodeType.external = v;
    return *this;
  }

  /**
   * @brief Mark this node as created by inttoptr cast
   * @param v If true, mark as inttoptr
   * @return Reference to this node (for chaining)
   */
  Node &setIntToPtr(bool v = true) {
    m_nodeType.inttoptr = v;
    return *this;
  }

  /**
   * @brief Mark this node as used in ptrtoint cast
   * @param v If true, mark as ptrtoint
   * @return Reference to this node (for chaining)
   */
  Node &setPtrToInt(bool v = true) {
    m_nodeType.ptrtoint = v;
    return *this;
  }

  /// @brief Check if this node represents a stack allocation
  bool isAlloca() const { return m_nodeType.alloca; }

  /// @brief Check if this node represents a heap allocation
  bool isHeap() const { return m_nodeType.heap; }

  /// @brief Check if this node has been read from
  bool isRead() const { return m_nodeType.read; }

  /// @brief Check if this node has been modified
  bool isModified() const { return m_nodeType.modified; }

  /// @brief Check if this node is externally visible
  bool isExternal() const { return m_nodeType.external; }

  /// @brief Check if this node was created by inttoptr
  bool isIntToPtr() const { return m_nodeType.inttoptr; }

  /// @brief Check if this node is used in ptrtoint
  bool isPtrToInt() const { return m_nodeType.ptrtoint; }

  /// @brief Check if this node has incomplete type information
  bool isIncomplete() const { return m_nodeType.incomplete; }

  /// @brief Check if this node has unknown allocation type
  bool isUnknown() const { return m_nodeType.unknown; }

  /**
   * @brief Set this node as an array with the given size
   *
   * Array nodes use modulo arithmetic for offset calculations.
   *
   * @param sz Size of the array in bytes
   * @return Reference to this node
   */
  Node &setArraySize(unsigned sz) {
    assert(!isArray());
    assert(!isForwarding());
    assert(m_size <= sz);

    setArray(true);
    m_size = sz;
    return *this;
  }

  Node &setArraySize(unsigned sz, boost::optional<unsigned>,
                     boost::optional<unsigned>) {
    return setArraySize(sz);
  }

  /// @brief Check if this node represents an array
  bool isArray() const { return m_nodeType.array; }

  /**
   * @brief Mark this node as offset-collapsed (field-insensitive)
   *
   * All offsets will map to offset 0. Loses field sensitivity.
   *
   * @param v If true, collapse offsets
   * @return Reference to this node
   */
  Node &setOffsetCollapsed(bool v = true) {
    m_nodeType.offset_collapsed = v;
    setArray(false);
    return *this;
  }

  /// @brief Check if this node has collapsed offsets
  bool isOffsetCollapsed() const { return m_nodeType.offset_collapsed; }

  /**
   * @brief Mark this node as type-collapsed
   *
   * All types at each offset are merged. Loses type-based field sensitivity.
   *
   * @param v If true, collapse types
   * @return Reference to this node
   */
  Node &setTypeCollapsed(bool v = true) {
    m_nodeType.type_collapsed = v;
    return *this;
  }

  /// @brief Check if this node has collapsed types
  bool isTypeCollapsed() const { return m_nodeType.type_collapsed; }

  bool isForeign() const { return m_nodeType.foreign; }

  bool areChunksShownCollapsed() const;

  Node &setForeign(bool v = true) {
    m_nodeType.foreign = v;
    return *this;
  }

  /// @brief Check if this is a unique node (has exactly one scalar)
  bool isUnique() const { return m_unique_scalar; }

  /// @brief Get the unique scalar if this node is unique
  const llvm::Value *getUniqueScalar() const { return m_unique_scalar; }

  /**
   * @brief Set the unique scalar for this node
   * @param v The unique scalar value
   */
  void setUniqueScalar(const llvm::Value *v) {
    m_unique_scalar = v;
    m_has_once_unique_scalar = true;
  }

  /// @brief Check if this node has ever had a unique scalar
  bool hasOnceUniqueScalar() const { return m_has_once_unique_scalar; }

  /**
   * @brief Merge unique scalars during simulation
   *
   * @param n Node to merge with
   * @return Bitmask: 0x1 if this changed, 0x2 if n changed, 0x3 if both
   * changed, 0x0 if no change
   */
  unsigned mergeUniqueScalar(Node &n);

  /**
   * @brief Merge unique scalars with caching
   * @tparam Cache Type of the seen cache
   * @param n Node to merge with
   * @param seen Cache to avoid redundant work
   * @return Change bitmask (same as above)
   */
  template <typename Cache> unsigned mergeUniqueScalar(Node &n, Cache &seen);

  /// @brief Check if this node is a forwarding pointer
  inline bool isForwarding() const;

  /// @brief Get the forwarding destination
  inline Cell &getForwardDest();

  /// @brief Get the forwarding destination (const)
  inline const Cell &getForwardDest() const;

  /// @brief Get the globally unique ID for this node
  uint64_t getId() const { return m_id; }

  /// @brief Get the parent graph
  Graph *getGraph() { return m_graph; }

  /// @brief Get the parent graph (const)
  const Graph *getGraph() const { return m_graph; }

  /**
   * @brief Get the actual non-forwarding node this represents
   *
   * Follows forwarding chains to find the real node. May be expensive.
   *
   * @return Pointer to the non-forwarding node
   */
  inline Node *getNode();

  /**
   * @brief Get the actual non-forwarding node this represents (const)
   * @return Pointer to the non-forwarding node
   */
  inline const Node *getNode() const;

  /// @brief Get the raw offset in the forwarding chain
  unsigned getRawOffset() const;

  /// @brief Get the accessed types map (mutable)
  accessed_types_type &types() { return m_accessedTypes; }

  /// @brief Get the accessed types map (const)
  const accessed_types_type &types() const { return m_accessedTypes; }

  /// @brief Get the links map (mutable)
  links_type &links() { return m_links; }

  /// @brief Get the links map (const)
  const links_type &links() const { return m_links; }

  /// @brief Get the size of this node in bytes
  unsigned size() const { return m_size; }

  /**
   * @brief Grow the size of this node to accommodate new offset
   * @param v New minimum size
   */
  void growSize(unsigned v);

  /**
   * @brief Check if a link exists at the given field
   * @param f The field to check
   * @return true if a link exists
   */
  bool hasLink(Field f) const {
    assert(g_IsTypeAware || f.getType().isUnknown());
    return m_links.count(Offset::getAdjustedField(*this, f));
  }

  /// @brief Get the total number of links in this node
  unsigned getNumLinks() const { return m_links.size(); }

  /// @brief Get the total number of chunks in this node
  unsigned getNumChunks() const { return m_chunks.size(); }
  unsigned getNumCollapsedCells() const { return getNumChunks(); }

  /**
   * @brief Get the cell pointed to by a field
   * @param f The field
   * @return The target cell
   */
  const Cell &getLink(Field f) const;

  /**
   * @brief Set the link at a field to point to a cell
   * @param _f The field
   * @param c The target cell
   */
  void setLink(const Field _f, const Cell &c);

  /**
   * @brief Add/merge a link at a field
   * @param field The field
   * @param c The cell to link to (unified with existing link if present)
   */
  void addLink(Field field, const Cell &c);

  /**
   * @brief Check if any type has been accessed at an offset
   * @param offset Byte offset
   * @return true if types exist at this offset
   */
  bool hasAccessedType(unsigned offset) const;

  /**
   * @brief Get the set of types accessed at an offset
   * @param o Byte offset
   * @return Immutable set of types
   */
  const Set getAccessedType(unsigned o) const {
    Offset offset(*this, o);
    auto it = m_accessedTypes.find(offset.getNumericOffset());
    assert(it != m_accessedTypes.end());
    return it->second;
  }

  /// @brief Check if no types have been accessed (void node)
  bool isVoid() const { return m_accessedTypes.empty(); }

  /// @brief Check if accessed type set is empty
  bool isEmtpyAccessedType() const;

  bool isPartialCollapsed() const { return m_chunks.size() >= 1; }

  /**
   * @brief Check if this node originates from null pointer operations
   *
   * E.g., gep(null, offset) creates a null allocation node.
   *
   * @return true if this is a null allocation
   */
  bool isNullAlloc() const { return m_nodeType.null; }

  /**
   * @brief Mark this node as a null allocation
   * @param v If true, mark as null allocation
   * @return Reference to this node
   */
  Node &setNullAlloc(bool v = true) {
    m_nodeType.null = v;
    return *this;
  }

  /**
   * @brief Add a type accessed at an offset
   * @param offset Byte offset
   * @param t LLVM type accessed at this offset
   */
  void addAccessedType(unsigned offset, llvm::Type *t);

  /**
   * @brief Collapse all offsets in this node
   *
   * Loses all field sensitivity. All accesses map to offset 0.
   *
   * @param tag Debug tag for tracking collapse reasons
   */
  void collapseOffsets(int tag /*= -2*/);

  /**
   * @brief Partially collapse offsets in a given range
   *
   * Merges all fields in [start, end] into a single one.
   *
   * @param start Start byte offset (inclusive)
   * @param end End byte offset (inclusive)
   * @param tag Debug tag for tracking collapse reasons
   */
  void partialCollapseOffsets(unsigned start, boost::optional<unsigned> end,
                              int tag /*= -2*/);

  /**
   * @brief Collapse all type distinctions in this node
   *
   * Loses type-based field sensitivity. All types at each offset are merged.
   *
   * @param tag Debug tag for tracking collapse reasons
   */
  void collapseTypes(int tag /*= -2*/);

  /**
   * @brief Add a new allocation site to this node
   * @param v The allocation site (instruction or global)
   */
  void addAllocSite(const DsaAllocSite &v);

  /**
   * @brief Get all allocation sites for this node
   * @return Set of allocation site values
   */
  const AllocaSet &getAllocSites() const { return m_alloca_sites; }

  /// @brief Clear all allocation sites
  void resetAllocSites() { m_alloca_sites.clear(); }

  /**
   * @brief Insert multiple allocation sites
   * @tparam Iterator Input iterator type
   * @param begin Start of range
   * @param end End of range
   */
  template <typename Iterator>
  void insertAllocSites(Iterator begin, Iterator end) {
    m_alloca_sites.insert(begin, end);
  }

  /**
   * @brief Check if a value is an allocation site for this node
   * @param v The value to check
   * @return true if v is an allocation site
   */
  bool hasAllocSite(const llvm::Value &v) { return m_alloca_sites.count(&v); }

  /**
   * @brief Join allocation sites from another set
   * @param s Set of allocation sites to merge
   */
  void joinAllocSites(const AllocaSet &s);

  /**
   * @brief Merge allocation sites during simulation
   * @param n Node to merge with
   * @return Change bitmask (0x1=this changed, 0x2=n changed, 0x3=both,
   * 0x0=none)
   */
  unsigned mergeAllocSites(Node &n);

  /**
   * @brief Merge allocation sites with caching
   * @tparam Cache Type of seen cache
   * @param n Node to merge with
   * @param seen Cache to avoid redundant work
   * @return Change bitmask
   */
  template <typename Cache> unsigned mergeAllocSites(Node &n, Cache &seen);

  /**
   * @brief Merge ck into m_chunks maintaining disjoint chunks
   * @param ck The chunk to be added
   */
  void mergeChunkIntoSet(Chunk &ck);

  /**
   * @brief Pretty-print this node to an output stream
   * @param o Output stream
   */
  void write(llvm::raw_ostream &o) const;

  /// @brief Dump node to stderr (for use in debugger)
  void dump() const;

  /// @brief Visualize this node's graph using GraphViz (for debugging)
  void viewGraph();
};

/// @brief Check if node is forwarding (inline implementation)
inline bool Node::isForwarding() const { return !m_forward.isNull(); }

/// @brief Get forwarding destination (inline implementation)
inline Cell &Node::getForwardDest() { return m_forward; }

/// @brief Get forwarding destination const (inline implementation)
inline const Cell &Node::getForwardDest() const { return m_forward; }

/**
 * @brief Check if cell has a link at offset (inline implementation)
 * @param offset Field offset to check
 * @return true if link exists
 */
inline bool Cell::hasLink(Field offset) const {
  return m_node && getNode()->hasLink(offset.addOffset(m_offset));
}

/**
 * @brief Get link at offset (inline implementation)
 * @param offset Field offset
 * @return The target cell
 */
inline const Cell &Cell::getLink(Field offset) const {
  assert(m_node);
  // -- call Node::getLink() const
  return static_cast<const Node *>(getNode())->getLink(
      offset.addOffset(m_offset));
}

/**
 * @brief Set link at offset (inline implementation)
 * @param offset Field offset
 * @param c Target cell
 */
inline void Cell::setLink(Field offset, const Cell &c) {
  getNode()->setLink(offset.addOffset(m_offset), c);
}

/**
 * @brief Add link at offset (inline implementation)
 * @param offset Field offset
 * @param c Cell to add/unify
 */
inline void Cell::addLink(Field offset, const Cell &c) {
  getNode()->addLink(offset.addOffset(m_offset), c);
}

/**
 * @brief Add accessed type (inline implementation)
 * @param offset Byte offset
 * @param t Type accessed
 */
inline void Cell::addAccessedType(unsigned offset, llvm::Type *t) {
  getNode()->addAccessedType(m_offset + offset, t);
}

/**
 * @brief Grow cell size (inline implementation)
 * @param o Offset
 * @param t Type at offset
 */
inline void Cell::growSize(unsigned o, llvm::Type *t) {
  assert(!isNull());
  Node::Offset offset(*getNode(), m_offset + o);
  getNode()->growSize(offset, t);
}

/**
 * @brief Get non-forwarding node (inline implementation)
 *
 * Follows forwarding chain to find actual node.
 *
 * @return Pointer to actual node
 */
inline Node *Node::getNode() {
  return isForwarding() ? m_forward.getNode() : this;
}

/**
 * @brief Get non-forwarding node const (inline implementation)
 * @return Pointer to actual node
 */
inline const Node *Node::getNode() const {
  return isForwarding() ? m_forward.getNode() : this;
}

inline void Chunk::addLink(Field f, const Cell &c) {
  Field cf(getStartOffset(), f.getType());
  getNode()->addLink(cf, c);
}

} // namespace seadsa

/**
 * @namespace llvm
 * @brief Extension of llvm namespace for DSA stream operators
 */
namespace llvm {

/// @brief Stream insertion operator for Node
inline raw_ostream &operator<<(raw_ostream &o, const seadsa::Node &n) {
  n.write(o);
  return o;
}

/// @brief Stream insertion operator for Cell
inline raw_ostream &operator<<(raw_ostream &o, const seadsa::Cell &c) {
  c.write(o);
  return o;
}

/// @brief Stream insertion operator for Chunk
inline raw_ostream &operator<<(raw_ostream &o, const seadsa::Chunk &c) {
  c.write(o);
  return o;
}

} // namespace llvm

/**
 * @namespace std
 * @brief Extension of std namespace for DSA hash functions
 */
namespace std {

/**
 * @brief Hash specialization for seadsa::Cell
 *
 * Allows Cell objects to be used as keys in unordered containers.
 */
template <> struct hash<seadsa::Cell> {
  /**
   * @brief Compute hash for a Cell
   * @param c The cell to hash
   * @return Hash value based on node pointer and offset
   */
  size_t operator()(const seadsa::Cell &c) const {
    size_t seed = 0;
    boost::hash_combine(seed, c.getNode());
    boost::hash_combine(seed, c.getRawOffset());
    // boost::hash_combine(seed, c.getType().asTuple());
    return seed;
  }
};
} // namespace std
