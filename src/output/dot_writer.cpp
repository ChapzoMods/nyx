// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/dot_writer.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/pseudo_c.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

namespace nyx::output {

namespace {

/// Escapes a string for safe inclusion inside a DOT double-quoted label.
/// Handles backslash, double-quote, newline and tab.
std::string escape_dot(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\l";  break;  // left-justified line break
            case '\t': out += "    "; break;
            default:   out += c;
        }
    }
    return out;
}

/// Returns a stable node id for a block start address.
std::string node_id(std::uint64_t addr, std::uint64_t fn_entry) {
    std::ostringstream s;
    s << "bb_" << std::hex << fn_entry << "_" << addr << std::dec;
    return s.str();
}

/// Builds the label for a single basic block: header line + one line per
/// IR instruction (rendered via the pseudo-C renderer).
std::string block_label(const ir::BasicBlock& b) {
    std::ostringstream s;
    s << "L_" << std::hex << b.start_addr << std::dec << "\\l";
    for (const auto& ins : b.instructions) {
        s << "  " << escape_dot(render_instruction(ins)) << "\\l";
    }
    return s.str();
}

}  // namespace

void write_dot(std::ostream& os, const ir::Function& fn) {
    os << "digraph \"" << escape_dot(fn.name.empty() ? std::string("sub") : fn.name) << "\" {\n";
    os << "  rankdir=TB;\n";
    os << "  node [shape=box, fontname=\"monospace\", fontsize=10];\n";
    os << "  edge [fontname=\"monospace\", fontsize=9];\n\n";

    // Emit nodes.
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        os << "  " << node_id(b.start_addr, fn.entry)
           << " [label=\"" << block_label(b) << "\"];\n";
    }
    os << "\n";

    // Emit edges. For each block, look at its terminator to decide the
    // edge label.
    std::unordered_map<std::uint64_t, std::size_t> by_addr;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        by_addr[fn.blocks[i].start_addr] = i;
    }

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        const auto src_id = node_id(b.start_addr, fn.entry);

        // Determine edge labels from the terminator.
        const ir::Instruction* last = b.instructions.empty() ? nullptr : &b.instructions.back();
        std::string cond_label;
        std::uint64_t branch_target = 0;
        bool has_branch = false;
        if (last) {
            if (last->op == ir::OpCode::BranchCond && last->operands.size() >= 2
                && last->operands[1].kind == ir::Operand::Kind::Label) {
                branch_target = last->operands[1].label_addr;
                has_branch = true;
                cond_label = "cond " + render_operand(last->operands[0]);
            } else if (last->op == ir::OpCode::Branch && !last->operands.empty()
                       && last->operands[0].kind == ir::Operand::Kind::Label) {
                branch_target = last->operands[0].label_addr;
                has_branch = true;
                cond_label = "";
            }
        }

        for (std::size_t j = 0; j < b.successors.size(); ++j) {
            const auto succ_addr = b.successors[j];
            auto it = by_addr.find(succ_addr);
            if (it == by_addr.end()) continue;
            const auto dst_id = node_id(succ_addr, fn.entry);
            std::string label;
            if (has_branch && succ_addr == branch_target && !cond_label.empty()) {
                label = cond_label;
            } else if (has_branch && succ_addr != branch_target) {
                label = "fall-through";
            } else if (has_branch && succ_addr == branch_target) {
                label = "";  // unconditional branch, no label
            } else {
                label = "fall-through";
            }
            os << "  " << src_id << " -> " << dst_id;
            if (!label.empty()) {
                os << " [label=\"" << escape_dot(label) << "\"]";
            }
            os << ";\n";
        }
    }

    os << "}\n";
}

void write_dot(std::ostream& os, const ir::Function& fn,
               const ir::DominatorAnalysis& dom,
               const std::vector<ir::NaturalLoop>& loops) {
    os << "digraph \"" << escape_dot(fn.name.empty() ? std::string("sub") : fn.name) << "\" {\n";
    os << "  rankdir=TB;\n";
    os << "  node [shape=box, fontname=\"monospace\", fontsize=10];\n";
    os << "  edge [fontname=\"monospace\", fontsize=9];\n\n";

    // Find entry block index.
    std::size_t entry_idx = 0;
    auto eit = fn.block_index.find(fn.entry);
    if (eit != fn.block_index.end()) entry_idx = eit->second;

    // Build loop-membership lookup: block -> loop index (header).
    std::unordered_map<std::size_t, std::size_t> loop_header_of;
    std::unordered_map<std::size_t, bool> in_any_loop;
    std::unordered_map<std::size_t, bool> is_loop_header;
    for (std::size_t li = 0; li < loops.size(); ++li) {
        for (std::size_t bi : loops[li].body) {
            loop_header_of[bi] = li;
            in_any_loop[bi] = true;
        }
        is_loop_header[loops[li].header] = true;
    }

    // Emit nodes with colour hints.
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        std::string color;
        std::string style = "filled";
        if (i == entry_idx) {
            color = "lightgreen";
        } else if (!dom.reachable(i)) {
            color = "lightgrey";
        } else if (is_loop_header.count(i)) {
            color = "lightyellow";
        } else if (in_any_loop.count(i)) {
            color = "lightblue";
        }
        os << "  " << node_id(b.start_addr, fn.entry)
           << " [label=\"" << block_label(b) << "\"";
        if (!color.empty()) {
            os << ", style=" << style << ", fillcolor=\"" << color << "\"";
        }
        os << "];\n";
    }
    os << "\n";

    // Build back-edge set: (from, to) pairs that are loop back edges.
    std::unordered_map<std::uint64_t, std::uint64_t> back_edges;
    for (const auto& loop : loops) {
        // The back edge is latch -> header.
        if (loop.latch < fn.blocks.size() && loop.header < fn.blocks.size()) {
            back_edges[fn.blocks[loop.latch].start_addr] = fn.blocks[loop.header].start_addr;
        }
    }

    // Emit edges.
    std::unordered_map<std::uint64_t, std::size_t> by_addr;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        by_addr[fn.blocks[i].start_addr] = i;
    }

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        const auto src_id = node_id(b.start_addr, fn.entry);
        const ir::Instruction* last = b.instructions.empty() ? nullptr : &b.instructions.back();
        std::string cond_label;
        std::uint64_t branch_target = 0;
        bool has_branch = false;
        if (last) {
            if (last->op == ir::OpCode::BranchCond && last->operands.size() >= 2
                && last->operands[1].kind == ir::Operand::Kind::Label) {
                branch_target = last->operands[1].label_addr;
                has_branch = true;
                cond_label = "cond " + render_operand(last->operands[0]);
            } else if (last->op == ir::OpCode::Branch && !last->operands.empty()
                       && last->operands[0].kind == ir::Operand::Kind::Label) {
                branch_target = last->operands[0].label_addr;
                has_branch = true;
                cond_label = "";
            } else if (last->indirect) {
                // Indirect branch/call: emit a comment edge to a virtual node.
                os << "  " << src_id << " -> \"indirect\" [label=\"indirect: "
                   << escape_dot(render_operand(last->operands[0])) << "\", style=dotted];\n";
                continue;
            }
        }

        for (std::size_t j = 0; j < b.successors.size(); ++j) {
            const auto succ_addr = b.successors[j];
            auto it = by_addr.find(succ_addr);
            if (it == by_addr.end()) continue;
            const auto dst_id = node_id(succ_addr, fn.entry);
            std::string label;
            std::string style;
            // Check if this is a back edge.
            auto be = back_edges.find(b.start_addr);
            if (be != back_edges.end() && be->second == succ_addr) {
                label = "back edge";
                style = "dashed";
            } else if (has_branch && succ_addr == branch_target && !cond_label.empty()) {
                label = cond_label;
            } else if (has_branch && succ_addr != branch_target) {
                label = "fall-through";
            } else if (has_branch && succ_addr == branch_target) {
                label = "";
            } else {
                label = "fall-through";
            }
            os << "  " << src_id << " -> " << dst_id;
            if (!label.empty() || !style.empty()) {
                os << " [";
                if (!label.empty()) os << "label=\"" << escape_dot(label) << "\"";
                if (!label.empty() && !style.empty()) os << ", ";
                if (!style.empty()) os << "style=" << style;
                os << "]";
            }
            os << ";\n";
        }
    }

    // Legend - documents the colour scheme used by the node fills and the
    // dashed back-edge style. Emitted as a single HTML-like label node so
    // Graphviz renders it as a small table outside the CFG.
    os << "\n  // Legend\n";
    os << "  legend [shape=none, label=<\n";
    os << "    <TABLE border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n";
    os << "      <TR><TD colspan=\"2\"><B>Legend</B></TD></TR>\n";
    os << "      <TR><TD bgcolor=\"lightgreen\">Entry</TD><TD>Function entry block</TD></TR>\n";
    os << "      <TR><TD bgcolor=\"lightyellow\">Loop header</TD><TD>Loop entry point</TD></TR>\n";
    os << "      <TR><TD bgcolor=\"lightblue\">Loop body</TD><TD>Block inside a loop</TD></TR>\n";
    os << "      <TR><TD bgcolor=\"lightgrey\">Unreachable</TD><TD>Pruned (dead code)</TD></TR>\n";
    os << "      <TR><TD>---</TD><TD>dashed = back edge</TD></TR>\n";
    os << "    </TABLE>\n";
    os << "  >];\n";

    os << "}\n";
}

std::string to_dot(const ir::Function& fn,
                   const ir::DominatorAnalysis& dom,
                   const std::vector<ir::NaturalLoop>& loops) {
    std::ostringstream os;
    write_dot(os, fn, dom, loops);
    return os.str();
}

void write_dot(std::ostream& os, const std::vector<ir::Function>& fns) {
    if (fns.empty()) {
        os << "digraph empty {}\n";
        return;
    }
    if (fns.size() == 1) {
        write_dot(os, fns[0]);
        return;
    }
    os << "digraph nyx {\n";
    os << "  rankdir=TB;\n";
    os << "  node [shape=box, fontname=\"monospace\", fontsize=10];\n";
    os << "  edge [fontname=\"monospace\", fontsize=9];\n";
    // For multiple functions, just concatenate the per-function digraphs
    // inside a top-level wrapper. Graphviz handles subgraph clusters but
    // for v0.0.4 simplicity we emit independent node ids per function.
    for (const auto& fn : fns) {
        os << "\n  // ---- function " << fn.name << " ----\n";
        // Inline the per-function body without the wrapping digraph.
        std::ostringstream sub;
        write_dot(sub, fn);
        std::string s = sub.str();
        // Strip the "digraph ... {" header and trailing "}".
        auto first_nl = s.find('\n');
        if (first_nl != std::string::npos) s.erase(0, first_nl + 1);
        if (!s.empty() && s.back() == '}') s.pop_back();
        // Strip trailing newline.
        if (!s.empty() && s.back() == '\n') s.pop_back();
        os << s << "\n";
    }
    os << "}\n";
}

std::string to_dot(const ir::Function& fn) {
    std::ostringstream os;
    write_dot(os, fn);
    return os.str();
}

std::string to_dot(const std::vector<ir::Function>& fns) {
    std::ostringstream os;
    write_dot(os, fns);
    return os.str();
}

}  // namespace nyx::output
