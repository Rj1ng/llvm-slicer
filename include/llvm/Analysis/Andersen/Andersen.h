//
// This file defines an implementation of Andersen's interprocedural alias
// analysis
//
// In pointer analysis terms, this is a subset-based, flow-insensitive,
// field-sensitive, and context-insensitive algorithm pointer algorithm.
//
// This algorithm is implemented as three stages:
//   1. Object identification.
//   2. Inclusion constraint identification.
//   3. Offline constraint graph optimization
//   4. Inclusion constraint solving.
//
// The object identification stage identifies all of the memory objects in the
// program, which includes globals, heap allocated objects, and stack allocated
// objects.
//
// The inclusion constraint identification stage finds all inclusion constraints
// in the program by scanning the program, looking for pointer assignments and
// other statements that effect the points-to graph.  For a statement like "A =
// B", this statement is processed to indicate that A can point to anything that
// B can point to.  Constraints can handle copies, loads, and stores, and
// address taking.
//
// The offline constraint graph optimization portion includes offline variable
// substitution algorithms intended to compute pointer and location
// equivalences.  Pointer equivalences are those pointers that will have the
// same points-to sets, and location equivalences are those variables that
// always appear together in points-to sets.  It also includes an offline
// cycle detection algorithm that allows cycles to be collapsed sooner
// during solving.
//
// The inclusion constraint solving phase iteratively propagates the inclusion
// constraints until a fixed point is reached.  This is an O(N^3) algorithm.
//
// Function constraints are handled as if they were structs with X fields.
// Thus, an access to argument X of function Y is an access to node index
// getNode(Y) + X.  This representation allows handling of indirect calls
// without any issues.  To wit, an indirect call Y(a,b) is equivalent to
// *(Y + 1) = a, *(Y + 2) = b.
// The return node for a function is always located at getNode(F) +
// CallReturnPos. The arguments start at getNode(F) + CallArgPos.
//

#ifndef TCFS_ANDERSEN_H
#define TCFS_ANDERSEN_H

#include "llvm/Analysis/Andersen/Constraint.h"
#include "llvm/Analysis/Andersen/NodeFactory.h"
#include "llvm/Analysis/Andersen/PtsSet.h"
#include "llvm/Analysis/Andersen/DetectParametersPass.h"

#include "llvm/Pass.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/CallSite.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/Andersen/ObjectiveCBinary.h"

#include "llvm/Analysis/Andersen/ObjCCallHandler.h"

#include <vector>
#include <set>
#include <map>
#include <mutex>

#include <sparsehash/sparse_hash_set>

//This is ugly as hell, but I don't want to includ a file from another module....
namespace llvm {
    extern llvm::Instruction const *getSuccInBlock(llvm::Instruction const *const I);
}

class Andersen : public llvm::ModulePass {
public:
    typedef std::set<std::pair<const llvm::Function *, int64_t>> FunctionIntPairSet_t;
    typedef std::map<const llvm::Value *, FunctionIntPairSet_t> StackOffsetMap_t;
    typedef std::set<std::string> StringSet_t;
private:
    const llvm::DataLayout *dataLayout;

    // A factory object that knows how to manage AndersNodes
    AndersNodeFactory nodeFactory;

    // Constraints - This vector contains a list of all of the constraints identified by the program.
    std::vector<AndersConstraint> constraints;

    // This is the points-to graph generated by the analysis
    std::map<NodeIndex, AndersPtsSet> ptsGraph;

    std::unique_ptr<llvm::ObjectiveCBinary> MachO;
    std::map<const llvm::Value *, StringSet_t> ObjectTypes;
    std::deque<llvm::Instruction *> CallInstWorklist;
    std::deque<llvm::Function *> FunctionWorklist;
    std::unique_ptr<llvm::SimpleCallGraph> CallGraph;

    // Three main phases
    void identifyObjects(llvm::Module &);

    void collectConstraints(llvm::Module &);

    void optimizeConstraints();

    void solveConstraints();

    // Helper functions for constraint collection
    void collectConstraintsForGlobals(llvm::Module &);

    void collectConstraintsForInstruction(const llvm::Instruction *);

    void addGlobalInitializerConstraints(NodeIndex, const llvm::Constant *);

    void addConstraintForCall(llvm::ImmutableCallSite cs);

    bool addConstraintForExternalLibrary(llvm::ImmutableCallSite cs, const llvm::Function *f);

    void addArgumentConstraintForCall(llvm::ImmutableCallSite cs, const llvm::Function *f);

    void addConstraintsForConstIntToPtr(const llvm::Value *IntToPtr, const llvm::ConstantInt *Const);

    // Helper functions for constraint optimization
    NodeIndex getRefNodeIndex(NodeIndex n) const;

    NodeIndex getAdrNodeIndex(NodeIndex n) const;

    // For debugging
    void dumpConstraint(const AndersConstraint &) const;

    void dumpConstraints() const;

    void dumpConstraintsPlainVanilla() const;

    void dumpPtsGraphPlainVanilla() const;

    void addProtocolConstraints(std::string className, std::string protocolName);

    llvm::Module *Mod;

    llvm::raw_ostream *unhandledFunctions;


    StackOffsetMap_t stackOffsetMap;
    std::mutex constraintLock;

    std::mutex outputLock;
    std::mutex callInstLock;
    std::mutex unhandledLock;
    std::mutex typeLock;
    std::mutex paramLock;

    std::map<uint64_t, const llvm::Value*> ivarMap;
    std::map<const llvm::Value*, llvm::Value*> DummyMap;

    //Holds all created dummy objects that are used to help creating
    //a callgraph (IVARs and protocol definitions for example)
    google::sparse_hash_set<const llvm::Value*> dummyHelpers;
public:
    static char ID;

    Andersen();

    bool runOnModule(llvm::Module &M);

    void getAnalysisUsage(llvm::AnalysisUsage &AU) const;

    void releaseMemory();

    // Given a llvm pointer v,
    // - Return false if the analysis doesn't know where v points to. In other words, the client must conservatively assume v can points to everything.
    // - Return true otherwise, and the points-to set of v is put into the second argument.
    bool getPointsToSet(const llvm::Value *v, std::vector<const llvm::Value *> &ptsSet) const;

    // Put all allocation sites (i.e. all memory objects identified by the analysis) into the first arugment
    void getAllAllocationSites(std::vector<const llvm::Value *> &allocSites) const;

    void setType(const llvm::Value *V, llvm::StringRef Typename);

    bool getType(const llvm::Value *V, StringSet_t &Typename);

    friend class AndersenAA;

    llvm::ObjectiveCBinary &getMachO() { return *MachO; };

    std::vector<AndersConstraint> &getConstraints() { return constraints; };

    AndersNodeFactory &getNodeFactory() { return nodeFactory; };

    void addToWorklist(llvm::Instruction *v) {
        callInstLock.lock();
        CallInstWorklist.push_back(v);
        callInstLock.unlock();
    }

    void addToWorklist(llvm::Function *f) { FunctionWorklist.push_back(f); }

    void addConstraintsForCall(const llvm::Instruction *Inst, const llvm::Function *F);

    llvm::SimpleCallGraph &getCallGraph() { return *CallGraph; };

    void preserveRegisterValue(llvm::Instruction *CallInst, uint64_t RegNo);

    bool copyParameter(llvm::Instruction *CallInst, llvm::Function *F, uint64_t RegNo);

    llvm::Module &getModule() { return *Mod; };

    llvm::Instruction *findSetStackParameterInstruction(llvm::Instruction *CallInst,
                                                        llvm::DetectParametersPass::ParameterAccessPair_t Parameter,
                                                        int64_t StackSize, int64_t CopyInParent = 0);

    llvm::Instruction *findSetRegisterParameterInstruction(llvm::Instruction *CallInst,
                                                           llvm::DetectParametersPass::ParameterAccessPair_t Parameter);

    std::set<llvm::Value *> Blocks;

    bool isBlock(const llvm::Instruction *Inst, const llvm::Value *&B);

    void addBlock(llvm::Value *B);

    bool handleBlock(const llvm::Instruction *Call, const llvm::Value *Block);

    std::set<const llvm::Value *> handledAliases;

    bool findAliases(const llvm::Value *Address, bool Sharp = true, uint64_t SPIdx = 3);

    llvm::raw_ostream &getUnhandledStream() { return *unhandledFunctions; }

    StackOffsetMap_t &getStackOffsets() { return stackOffsetMap; }

    bool pointsTo(const llvm::Value *v, const llvm::Value *loc) {
        std::vector<const llvm::Value *> PtsTo;
        getPointsToSet(v, PtsTo);
        for (auto &pts_it : PtsTo) {
            if (pts_it == loc)
                return true;
        }
        return false;
    }

    inline void addConstraint(AndersConstraint::ConstraintType Ty, NodeIndex D, NodeIndex S) {
        constraintLock.lock();
        constraints.emplace_back(Ty, D, S);
        constraintLock.unlock();
    }

    std::mutex &getOutputLock() { return outputLock; }

    void addUnhandled(const std::string functionName, const llvm::Instruction *inst) {

//        *unhandledFunctions << "Can't handle call: " << functionName << " " <<
//        inst->getParent()->getParent()->getName() << ": ";
//        if (llvm::getSuccInBlock(inst)) { llvm::getSuccInBlock(inst)->print(*unhandledFunctions); }
//        *unhandledFunctions << "\n";

//        *unhandledFunctions << "Can't handle call: " << functionName << "\n";
        if (unhandledFunctions == &llvm::nulls()) {
            return;
        }


        unhandledLock.lock();
//        *unhandledFunctions << "Can't handle call: " << functionName << " ";
//        if (s)
//            s->print(*unhandledFunctions);
//        *unhandledFunctions << "\n";
        *unhandledFunctions << "Can't handle call: " << functionName << "\n";
        unhandledLock.unlock();
    }

    bool isDummyHelper(const llvm::Value *val);
};


#endif
