///
/// @file PhiLowering.cpp
/// @brief phi 降级 pass 实现
///
/// 参考：
///   Briggs, Cooper, Harvey, Simpson, "Practical Improvements to the
///   Construction and Destruction of Static Single Assignment Form",
///   Software—Practice & Experience, 1998.
///
///   Boissinot et al., "Revisiting Out-of-SSA Translation for Correctness,
///   Code Quality, and Efficiency", CGO 2009.
///

#include "PhiLowering.h"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "BasicBlock.h"
#include "CopyInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "PhiInst.h"
#include "Value.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

PhiLowering::PhiLowering(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void PhiLowering::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return;
    }

    // Collect phi nodes and their removals.
    // Iterate over a snapshot of the block list (phi removal may modify the
    // instruction list but not the block list).
    std::vector<PhiInst *> toDelete;

    for (auto * bb : func->getBlocks()) {
        // Collect phi nodes at the top of this block.
        std::vector<PhiInst *> phis;
        for (auto * inst : bb->getInstructions()) {
            if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                phis.push_back(phi);
            } else {
                break; // phi nodes are always at the top of a block
            }
        }
        if (phis.empty()) {
            continue;
        }

        // Build per-predecessor parallel-copy sets:
        //   predCopies[pred] = { (phi_result*, incoming_value*) ... }
        std::unordered_map<BasicBlock *, std::vector<std::pair<Value *, Value *>>> predCopies;
        for (auto * phi : phis) {
            for (int i = 0; i < phi->getIncomingCount(); ++i) {
                const auto & inc = phi->getIncoming(i);
                // dst = phi itself (as Value*); src = incoming value
                predCopies[inc.block].emplace_back(static_cast<Value *>(phi), inc.value);
            }
        }

        // Insert sequential copies in each predecessor.
        for (auto & [pred, copies] : predCopies) {
            insertSequentialCopies(pred, copies);
        }

        // Remove phi instructions from BB (defer deletion to avoid use-after-free
        // while the loop above still reads phi->getIncoming(...)).
        auto & insts = bb->getInstructions();
        for (auto * phi : phis) {
            phi->clearOperands(); // unlink from use-def chain
            insts.remove(phi);
            toDelete.push_back(phi);
        }
    }

    // 不在此处删除phi对象。生成的CopyInst节点使用phi对象本身作为逻辑目标Value，
    // 在out-of-SSA降级后，后续指令仍会引用该Value。
    (void) toDelete;
}

// ---------------------------------------------------------------------------
// Parallel-copy serialisation (Briggs-Cooper worklist)
// ---------------------------------------------------------------------------
//
// Given a set of parallel copies { (dst_i, src_i) }, produce an equivalent
// sequential list of copies that correctly handles cycles (e.g., swap).
//
// Key invariant:
//   A copy (dst, src) is "ready" iff dst does NOT appear as the src of any
//   other PENDING copy.  (If it did, emitting (dst := src) first would
//   clobber the value that the other pending copy still needs to read.)
//
// When the worklist contains only cyclic copies, break one cycle by:
//   1. Pick any copy (dst0, src0) from the cycle.
//   2. Insert a fresh temporary copy: %tmp = copy dst0.
//   3. Replace every pending src == dst0 with %tmp.
//   4. (dst0, src0) is now "ready" – add it to the ready queue.
//
// ---------------------------------------------------------------------------

void PhiLowering::insertSequentialCopies(BasicBlock * pred,
                                          std::vector<std::pair<Value *, Value *>> & copies)
{
    // ---- Filter identity copies (dst == src) --------------------------------
    copies.erase(std::remove_if(copies.begin(),
                                copies.end(),
                                [](const std::pair<Value *, Value *> & p) {
                                    return p.first == p.second;
                                }),
                 copies.end());

    if (copies.empty()) {
        return;
    }

    // ---- Build per-element status trackers ----------------------------------
    // pending[i] == true  →  copy i has not yet been emitted
    std::vector<bool> pending(copies.size(), true);

    // Count how many PENDING copies have copies[i].first as their src.
    // A copy is "ready" iff this count == 0 for its dst.
    std::unordered_map<Value *, int> dstUsedAsSrc;
    for (auto & [d, s] : copies) {
        (void) d;
        dstUsedAsSrc[s]; // ensure entry exists
    }
    for (auto & [d, s] : copies) {
        // s is used as src by this copy; but we care about whether d (the dst
        // of some copy) is ALSO a src of another copy.
        (void) s;
        (void) d;
    }

    // Recompute cleanly: for each copy i, track how many other pending copies
    // have copies[i].first (the dst of i) as their src.
    // dst_as_src_count[v] = number of pending copies whose src == v
    std::unordered_map<Value *, int> dstAsSrcCount;
    for (auto & [d, s] : copies) {
        dstAsSrcCount[d] += 0; // ensure entry
        dstAsSrcCount[s] += 1; // s is consumed by this copy
    }

    // ---- Find the insertion point: just before the terminator ---------------
    auto & insts = pred->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty()) {
        auto lastIt = std::prev(insts.end());
        if ((*lastIt)->isTerminator()) {
            insertPos = lastIt; // insert before terminator
        }
        // If there is no terminator (malformed block), we append at end.
    }

    auto insertCopy = [&](CopyInst * ci) {
        insts.insert(insertPos, ci);
        ci->setParentBlock(pred);
    };

    // ---- Worklist algorithm -------------------------------------------------
    //
    // ready: indices of copies that can be emitted now (dst not used as src)
    std::vector<int> readyQueue;
    for (int i = 0; i < static_cast<int>(copies.size()); ++i) {
        if (dstAsSrcCount[copies[i].first] == 0) {
            readyQueue.push_back(i);
        }
    }

    int emitted = 0;

    while (emitted < static_cast<int>(copies.size())) {

        // ---- Drain ready queue ----------------------------------------------
        while (!readyQueue.empty()) {
            int idx = readyQueue.back();
            readyQueue.pop_back();

            if (!pending[idx]) {
                continue; // already processed
            }

            auto [dst, src] = copies[idx];
            pending[idx] = false;
            ++emitted;

            // Emit: dst_phi_irname = copy src
            auto * ci = new CopyInst(func, src, dst);
            insertCopy(ci);

            // After we commit dst, any copy whose src == dst now has one fewer
            // "blocker": decrement and check if it becomes ready.
            auto it = dstAsSrcCount.find(dst);
            if (it != dstAsSrcCount.end()) {
                // 'dst' was consumed as a src by some copies; now that we have
                // finalised dst's value, those copies are unblocked.
                // Actually: we need to find which copies HAD dst as src and
                // recheck their own dst's count.
                // But the count tracks "how many pending copies read this value".
                // After emitting (dst := src), 'dst' is now *written*, meaning
                // no future copy can safely read the old 'dst'.
                // What we need: unblock copies whose DST was blocked because
                // their DST was ALSO used as a src (by someone else).
                // "Someone else" reading 'x' means (x in dstAsSrcCount and > 0).
                // After emitting the current copy, 'dst' is no longer an
                // original value; but other copies that had src == 'dst' in the
                // *original* set would have already been counted.
                // This is covered below by decrement logic.
            }

            // For each still-pending copy j where copies[j].second == dst,
            // decrement dstAsSrcCount[copies[j].first].
            // (Those copies were waiting because their dst was also a src;
            //  but we already process them via readyQueue logic.  What we
            //  actually need: after emitting copy (dst := src), the value
            //  'src' is no longer "needed as the original src for index idx",
            //  so we decrease the count for 'src' – but only by 1 since only
            //  this one copy consumed it.)
            //
            // More precisely: dstAsSrcCount[v] counts how many pending copies
            // have v as their src.  Emitting idx removes one such consumer of
            // 'src'.  If dstAsSrcCount[src] drops to 0, the copy whose dst
            // is 'src' (if any) becomes ready.
            dstAsSrcCount[src] -= 1;
            // Find the copy whose dst == src (if any) and add to ready if unblocked.
            for (int j = 0; j < static_cast<int>(copies.size()); ++j) {
                if (pending[j] && copies[j].first == src && dstAsSrcCount[src] == 0) {
                    readyQueue.push_back(j);
                }
            }
        }

        if (emitted == static_cast<int>(copies.size())) {
            break;
        }

        // ---- Cycle detected: break it with a temporary ----------------------
        // Find the first still-pending copy.
        int cycleIdx = -1;
        for (int i = 0; i < static_cast<int>(copies.size()); ++i) {
            if (pending[i]) {
                cycleIdx = i;
                break;
            }
        }
        if (cycleIdx == -1) {
            break; // should not happen
        }

        Value * cycleDst = copies[cycleIdx].first;

        // Emit a fresh temporary copy: %tmp = copy cycleDst (reads the CURRENT
        // value of cycleDst before it gets overwritten).
        auto * tmpCopy = new CopyInst(func, cycleDst); // fresh-value copy
        insertCopy(tmpCopy);

        // Replace all pending copies whose src == cycleDst with %tmp.
        for (int j = 0; j < static_cast<int>(copies.size()); ++j) {
            if (pending[j] && copies[j].second == cycleDst) {
                copies[j].second = static_cast<Value *>(tmpCopy);
                // dstAsSrcCount bookkeeping: cycleDst loses one consumer,
                // tmpCopy gains one.
                dstAsSrcCount[cycleDst] -= 1;
                dstAsSrcCount[static_cast<Value *>(tmpCopy)] += 1;
            }
        }

        // cycleDst's dst-as-src count may have dropped to 0, making it ready.
        if (dstAsSrcCount[cycleDst] == 0) {
            readyQueue.push_back(cycleIdx);
        }
    }
}
