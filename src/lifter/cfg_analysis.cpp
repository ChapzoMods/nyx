// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/cfg_analysis.hpp"

#include "nyx/core/logger.hpp"

#include <algorithm>
#include <queue>
#include <stack>

namespace nyx::ir {

// ---------------------------------------------------------------------------
// DominatorAnalysis queries
// ---------------------------------------------------------------------------
std::optional<std::size_t> DominatorAnalysis::immediate_dominator(std::size_t b) const noexcept {
    if (b >= idom.size()) return std::nullopt;
    const int d = idom[b];
    if (d < 0) return std::nullopt;  // entry or unreachable
    return static_cast<std::size_t>(d);
}

bool DominatorAnalysis::dominates(std::size_t a, std::size_t b) const noexcept {
    if (a >= idom.size() || b >= idom.size()) return false;
    if (a == b) return true;
    // Walk the dominator chain from b up to the entry; if we hit a, a dom b.
    std::size_t cur = b;
    // Guard against cycles with a visit cap.
    for (std::size_t i = 0; i < idom.size() + 1; ++i) {
        const int d = idom[cur];
        if (d < 0) return false;  // reached entry or unreachable
        if (static_cast<std::size_t>(d) == a) return true;
        cur = static_cast<std::size_t>(d);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reverse postorder traversal of reachable blocks.
// ---------------------------------------------------------------------------
namespace {

std::vector<std::size_t> compute_rpo(const Function& fn, std::size_t entry_idx) {
    // DFS postorder, then reverse.
    std::vector<std::size_t> post;
    std::vector<bool> visited(fn.blocks.size(), false);
    std::stack<std::pair<std::size_t, bool>> stk;  // (block, expanded)
    stk.push({entry_idx, false});

    while (!stk.empty()) {
        auto [idx, expanded] = stk.top();
        stk.pop();
        if (idx >= fn.blocks.size()) continue;
        if (expanded) {
            post.push_back(idx);
            continue;
        }
        if (visited[idx]) continue;
        visited[idx] = true;
        stk.push({idx, true});  // will be popped after children
        // Push successors (reverse order so first successor is processed first).
        const auto& succs = fn.blocks[idx].successors;
        for (auto it = succs.rbegin(); it != succs.rend(); ++it) {
            auto sit = fn.block_index.find(*it);
            if (sit != fn.block_index.end() && !visited[sit->second]) {
                stk.push({sit->second, false});
            }
        }
    }
    std::reverse(post.begin(), post.end());
    return post;
}

}  // namespace

// ---------------------------------------------------------------------------
// compute_dominators: iterative dataflow (Cooper-Harvey-Kennedy)
// ---------------------------------------------------------------------------
DominatorAnalysis compute_dominators(const Function& fn) {
    DominatorAnalysis dom;
    if (fn.blocks.empty()) return dom;

    // Find entry block index.
    auto eit = fn.block_index.find(fn.entry);
    if (eit == fn.block_index.end()) {
        // Entry not in block_index - fall back to block 0.
        eit = fn.block_index.find(fn.blocks[0].start_addr);
    }
    if (eit == fn.block_index.end()) return dom;
    const std::size_t entry = eit->second;

    dom.rpo = compute_rpo(fn, entry);
    if (dom.rpo.empty()) return dom;

    // rpo_map[block_idx] = position in rpo (smaller = earlier).
    std::vector<int> rpo_pos(fn.blocks.size(), -1);
    for (std::size_t i = 0; i < dom.rpo.size(); ++i) {
        rpo_pos[dom.rpo[i]] = static_cast<int>(i);
    }

    // idom initialized to -1 (undefined). Entry's idom is itself (-2 sentinel).
    dom.idom.assign(fn.blocks.size(), -1);
    dom.idom[entry] = -2;  // entry has no dominator; we use -2 to mark "entry".

    // Helper: intersect two dominator sets via the RPO-position walk.
    auto intersect = [&](std::size_t b1, std::size_t b2) -> std::size_t {
        int finger1 = rpo_pos[b1];
        int finger2 = rpo_pos[b2];
        while (finger1 != finger2) {
            while (finger1 > finger2) {
                const int d = dom.idom[dom.rpo[finger1]];
                if (d < 0) break;
                finger1 = rpo_pos[d];
            }
            while (finger2 > finger1) {
                const int d = dom.idom[dom.rpo[finger2]];
                if (d < 0) break;
                finger2 = rpo_pos[d];
            }
        }
        return dom.rpo[finger1];
    };

    // Build predecessor lists (only among reachable blocks).
    std::vector<std::vector<std::size_t>> preds(fn.blocks.size());
    for (std::size_t i : dom.rpo) {
        for (auto succ_addr : fn.blocks[i].successors) {
            auto sit = fn.block_index.find(succ_addr);
            if (sit != fn.block_index.end()) {
                preds[sit->second].push_back(i);
            }
        }
    }

    // Iterate until no change.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i : dom.rpo) {
            if (i == entry) continue;
            // Find first processed predecessor.
            std::size_t new_idom = static_cast<std::size_t>(-1);
            for (std::size_t p : preds[i]) {
                if (dom.idom[p] == -1) continue;  // unprocessed
                if (new_idom == static_cast<std::size_t>(-1)) {
                    new_idom = p;
                } else {
                    new_idom = intersect(p, new_idom);
                }
            }
            if (new_idom != static_cast<std::size_t>(-1)) {
                const int current = dom.idom[i];
                if (current < 0 || static_cast<std::size_t>(current) != new_idom) {
                    dom.idom[i] = static_cast<int>(new_idom);
                    changed = true;
                }
            }
        }
    }

    // Convert entry sentinel -2 to -1 for the public API (entry has no idom).
    dom.idom[entry] = -1;
    // Any block whose idom is still -1 and isn't the entry is unreachable.
    return dom;
}

// ---------------------------------------------------------------------------
// find_natural_loops
// ---------------------------------------------------------------------------
std::vector<NaturalLoop> find_natural_loops(const Function& fn, const DominatorAnalysis& dom) {
    std::vector<NaturalLoop> loops;

    // A back edge is B -> H where H dominates B.
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        if (!dom.reachable(b)) continue;
        for (auto succ_addr : fn.blocks[b].successors) {
            auto sit = fn.block_index.find(succ_addr);
            if (sit == fn.block_index.end()) continue;
            const std::size_t h = sit->second;
            if (!dom.reachable(h)) continue;
            if (!dom.dominates(h, b)) continue;
            // Back edge b -> h. Collect loop body.
            NaturalLoop loop;
            loop.header = h;
            loop.latch  = b;
            loop.body.push_back(h);
            // Walk backwards from b, collecting predecessors until we hit h.
            std::vector<bool> in_loop(fn.blocks.size(), false);
            in_loop[h] = true;
            std::vector<std::size_t> worklist = {b};
            in_loop[b] = true;
            while (!worklist.empty()) {
                const std::size_t y = worklist.back();
                worklist.pop_back();
                // Find predecessors of y.
                for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
                    if (!dom.reachable(i)) continue;
                    for (auto sa : fn.blocks[i].successors) {
                        auto sit2 = fn.block_index.find(sa);
                        if (sit2 == fn.block_index.end()) continue;
                        if (sit2->second != y) continue;
                        if (in_loop[i]) continue;
                        in_loop[i] = true;
                        worklist.push_back(i);
                    }
                }
            }
            for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
                if (in_loop[i]) loop.body.push_back(i);
            }
            loops.push_back(std::move(loop));
        }
    }

    return loops;
}

// ---------------------------------------------------------------------------
// reachable_blocks: BFS from entry
// ---------------------------------------------------------------------------
std::unordered_set<std::size_t> reachable_blocks(const Function& fn) {
    std::unordered_set<std::size_t> reach;
    if (fn.blocks.empty()) return reach;

    auto eit = fn.block_index.find(fn.entry);
    if (eit == fn.block_index.end()) {
        eit = fn.block_index.find(fn.blocks[0].start_addr);
    }
    if (eit == fn.block_index.end()) return reach;
    const std::size_t entry = eit->second;

    std::queue<std::size_t> q;
    q.push(entry);
    reach.insert(entry);
    while (!q.empty()) {
        const std::size_t b = q.front();
        q.pop();
        for (auto succ_addr : fn.blocks[b].successors) {
            auto sit = fn.block_index.find(succ_addr);
            if (sit == fn.block_index.end()) continue;
            if (reach.count(sit->second)) continue;
            reach.insert(sit->second);
            q.push(sit->second);
        }
    }
    return reach;
}

}  // namespace nyx::ir
