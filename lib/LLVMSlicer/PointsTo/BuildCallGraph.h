#include <llvm/Analysis/Andersen/Andersen.h>
#include <llvm/Analysis/Andersen/DetectParametersPass.h>
#include <llvm/Analysis/Andersen/SimpleCallGraph.h>
#include <map>
#include <set>
#include <vector>

#include "llvm/IR/Value.h"

#include "../Backtrack/Constraint.h"
#include "./RuleExpressions.h"

namespace llvm {
namespace ptr {

class PointsToSets {
public:
  typedef const llvm::Value *MemoryLocation;
  /*
   * pointer is a pair <location, offset> such that the location is:
   * a) variable, offset is -1
   * b) alloc,    offset is <0,infty) -- structure members can point too
   *
   * Note that in LLVM, both a variable and an alloc (CallInst to malloc)
   * are llvm::Value.
   */
  typedef std::pair<MemoryLocation, int> Pointer;
  /*
   * Points-to set contains against pairs <location, offset>, where location
   * can be only an alloc companied by an offset (we can point to the
   * middle).
   */
  typedef std::pair<MemoryLocation, int> Pointee;
  typedef std::set<Pointee> PointsToSet;

  typedef std::map<Pointer, PointsToSet> Container;
  typedef Container::key_type key_type;
  typedef Container::mapped_type mapped_type;
  typedef Container::value_type value_type;
  typedef Container::iterator iterator;
  typedef Container::const_iterator const_iterator;
  typedef std::pair<iterator, bool> insert_retval;

  virtual ~PointsToSets() {}

  insert_retval insert(value_type const &val) { return C.insert(val); }
  PointsToSet &operator[](key_type const &key) { return C[key]; }
  const_iterator find(key_type const &key) const { return C.find(key); }
  iterator find(key_type const &key) { return C.find(key); }
  const_iterator begin() const { return C.begin(); }
  iterator begin() { return C.begin(); }
  const_iterator end() const { return C.end(); }
  iterator end() { return C.end(); }
  Container const &getContainer() const { return C; }
  Container &getContainer() { return C; }

private:
  Container C;
};

} // namespace ptr
} // namespace llvm

namespace llvm {
namespace ptr {
struct SimpleCallGraphInit {
  typedef RuleCode Command;
  typedef std::vector<Command> Container;
  typedef Container::value_type value_type;
  typedef Container::iterator iterator;
  typedef Container::const_iterator const_iterator;

  explicit SimpleCallGraphInit(Module &M);

  llvm::Module &getModule() const { return M; }

  void insert(iterator it, value_type const &val) { C.insert(it, val); }
  void push_back(value_type const &val) { return C.push_back(val); }
  const_iterator begin() const { return C.begin(); }
  iterator begin() { return C.begin(); }
  const_iterator end() const { return C.end(); }
  iterator end() { return C.end(); }
  Container const &getContainer() const { return C; }
  Container &getContainer() { return C; }

private:
  Container C;
  llvm::Module &M;
};

} // namespace ptr
} // namespace llvm

namespace llvm {
namespace ptr {
const PointsToSets::PointsToSet &
getPointsToSet(const llvm::Value *const &memLoc, const PointsToSets &S,
               const int offset = -1);

SimpleCallGraph &getSimpleCallGraph();
DetectParametersPass &getDetectParametersPass();
Andersen *getAndersen();

PointsToSets &computePointsToSets(const SimpleCallGraphInit &callgraph,
                                  PointsToSets &S);

} // namespace ptr
} // namespace llvm