// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/type_inferer.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/logger.hpp"
#include "nyx/parsers/dwarf_parser.hpp"

#include <algorithm>
#include <cstdint>

namespace nyx {

TypeInferer::TypeInferer(Arch arch, const BinaryInfo* bin)
    : arch_(arch), bin_(bin) {}

ir::Type TypeInferer::type_from_size(std::uint64_t size) noexcept {
    switch (size) {
        case 1:  return ir::Type::Int8;
        case 2:  return ir::Type::Int16;
        case 4:  return ir::Type::Int32;
        case 8:  return ir::Type::Int64;
        default: return ir::Type::Unknown;
    }
}

ir::Type TypeInferer::type_from_symbol(std::uint64_t addr) const noexcept {
    if (!bin_) return ir::Type::Unknown;
    for (const auto& s : bin_->symbols) {
        if (s.value == addr) {
            if (s.kind == Symbol::Kind::Function) return ir::Type::Func;
            return type_from_size(s.size);
        }
    }
    return ir::Type::Unknown;
}

ir::Type TypeInferer::type_from_imm(std::int64_t v) const noexcept {
    // Conservative: small immediates fit in Int32; only use Int64 when the
    // value actually needs more than 32 bits.
    if (v >= INT32_MIN && v <= INT32_MAX) return ir::Type::Int32;
    return ir::Type::Int64;
}

ir::Type TypeInferer::type_of(const ir::Function& fn, ir::VReg v) const noexcept {
    auto it = fn.vreg_types.find(v);
    if (it == fn.vreg_types.end()) return ir::Type::Unknown;
    return it->second;
}

void TypeInferer::infer(ir::Function& fn) const {
    fn.vreg_types.clear();

    // If the function has a name matching a symbol, seed its entry vreg
    // (if any) with the symbol's type.
    if (bin_) {
        for (const auto& s : bin_->symbols) {
            if (s.name == fn.name && s.kind == Symbol::Kind::Function) {
                // The function itself is Func-typed; we don't assign to a
                // vreg here but the renderer can use fn.name's type.
                (void)s;
                break;
            }
        }
    }

    for (const auto& block : fn.blocks) {
        for (const auto& ins : block.instructions) {
            ir::Type inferred = ir::Type::Unknown;

            switch (ins.op) {
                case ir::OpCode::Mov: {
                    if (ins.operands.empty()) break;
                    const auto& src = ins.operands[0];
                    if (src.kind == ir::Operand::Kind::Imm) {
                        inferred = type_from_imm(src.imm_value);
                    } else if (src.kind == ir::Operand::Kind::Symbol) {
                        // Look up the symbol by name.
                        if (bin_) {
                            for (const auto& s : bin_->symbols) {
                                if (s.name == src.symbol) {
                                    inferred = (s.kind == Symbol::Kind::Function)
                                             ? ir::Type::Func
                                             : type_from_size(s.size);
                                    break;
                                }
                            }
                        }
                        if (inferred == ir::Type::Unknown) {
                            // External symbol without size info: assume Func
                            // for call targets, Ptr otherwise.
                            inferred = ir::Type::Ptr;
                        }
                    } else if (src.kind == ir::Operand::Kind::Register) {
                        // reg = reg; inherit source type if known.
                        auto it = fn.vreg_types.find(src.vreg);
                        if (it != fn.vreg_types.end()) inferred = it->second;
                    } else if (src.kind == ir::Operand::Kind::Mem) {
                        // mov reg, [mem] is a Load (handled below in Load),
                        // but the generic Mov case still applies for LEA-
                        // style patterns where the lifter emitted Mov.
                        inferred = type_from_size(src.mem_size);
                    }
                    break;
                }
                case ir::OpCode::Load: {
                    if (ins.operands.empty()) break;
                    const auto& addr = ins.operands[0];
                    if (addr.kind == ir::Operand::Kind::Mem) {
                        // Prefer the explicit mem_size hint.
                        if (addr.mem_size != 0) {
                            inferred = type_from_size(addr.mem_size);
                        } else if (addr.mem_base != ir::INVALID_VREG) {
                            // Loading through a pointer vreg: if the base
                            // vreg is itself typed as Ptr, the load result
                            // is whatever the pointer points to. We can't
                            // know without points-to analysis, so default
                            // to Ptr (the result is itself address-sized).
                            inferred = ir::Type::Ptr;
                        }
                        // If the displacement matches a known symbol's
                        // address, infer from the symbol's size.
                        if (inferred == ir::Type::Unknown && bin_ && addr.mem_disp != 0) {
                            const auto t = type_from_symbol(static_cast<std::uint64_t>(addr.mem_disp));
                            if (t != ir::Type::Unknown) inferred = t;
                        }
                    }
                    break;
                }
                case ir::OpCode::Store: {
                    // Stores don't produce a value; skip.
                    break;
                }
                case ir::OpCode::Add: case ir::OpCode::Sub: case ir::OpCode::Mul:
                case ir::OpCode::Div: case ir::OpCode::Mod: case ir::OpCode::And:
                case ir::OpCode::Or:  case ir::OpCode::Xor: case ir::OpCode::Shl:
                case ir::OpCode::Shr: case ir::OpCode::Sar: case ir::OpCode::Neg:
                case ir::OpCode::Not: {
                    // Binary/unary op: result type is the wider of the two
                    // operands. If either is Unknown, fall back to Int32
                    // (most arithmetic fits).
                    if (ins.operands.size() >= 2) {
                        ir::Type a = ir::Type::Unknown;
                        ir::Type b = ir::Type::Unknown;
                        const auto& op0 = ins.operands[0];
                        const auto& op1 = ins.operands[1];
                        if (op0.kind == ir::Operand::Kind::Register) {
                            auto it = fn.vreg_types.find(op0.vreg);
                            if (it != fn.vreg_types.end()) a = it->second;
                        } else if (op0.kind == ir::Operand::Kind::Imm) {
                            a = type_from_imm(op0.imm_value);
                        }
                        if (op1.kind == ir::Operand::Kind::Register) {
                            auto it = fn.vreg_types.find(op1.vreg);
                            if (it != fn.vreg_types.end()) b = it->second;
                        } else if (op1.kind == ir::Operand::Kind::Imm) {
                            b = type_from_imm(op1.imm_value);
                        }
                        // Pick the "wider" type.
                        const auto wider = [](ir::Type x, ir::Type y) -> ir::Type {
                            const auto rank = [](ir::Type t) -> int {
                                switch (t) {
                                    case ir::Type::Unknown: return 0;
                                    case ir::Type::Int8:    return 1;
                                    case ir::Type::Int16:   return 2;
                                    case ir::Type::Int32:   return 3;
                                    case ir::Type::Int64:   return 4;
                                    case ir::Type::Ptr:     return 3;
                                    case ir::Type::Func:    return 3;
                                }
                                return 0;
                            };
                            return rank(x) >= rank(y) ? x : y;
                        };
                        inferred = wider(a, b);
                        if (inferred == ir::Type::Unknown) inferred = ir::Type::Int32;
                    } else {
                        inferred = ir::Type::Int32;
                    }
                    break;
                }
                case ir::OpCode::Cmp: {
                    // Comparisons produce a boolean-like result; model as
                    // Int32 (C int).
                    inferred = ir::Type::Int32;
                    break;
                }
                case ir::OpCode::Pop: {
                    // Pop result: pointer-sized.
                    inferred = ir::Type::Ptr;
                    break;
                }
                case ir::OpCode::Call: {
                    // Call produces a return value of unknown type; default
                    // to Int32 (most C functions return int).
                    inferred = ir::Type::Int32;
                    break;
                }
                case ir::OpCode::Branch:
                case ir::OpCode::BranchCond:
                case ir::OpCode::Return:
                case ir::OpCode::Push:
                case ir::OpCode::Nop:
                case ir::OpCode::Opaque:
                    // No destination vreg.
                    break;
            }

            if (ins.dst != ir::INVALID_VREG && inferred != ir::Type::Unknown) {
                // Don't downgrade an already-inferred type.
                auto it = fn.vreg_types.find(ins.dst);
                if (it == fn.vreg_types.end()) {
                    fn.vreg_types[ins.dst] = inferred;
                } else {
                    // Keep the more specific (non-Unknown) type.
                    if (it->second == ir::Type::Unknown) it->second = inferred;
                }
            }
        }
    }
}

ir::Type TypeInferer::type_from_dwarf(std::uint64_t type_offset) const noexcept {
    if (!bin_ || !bin_->dwarf) return ir::Type::Unknown;
    const auto& d = *bin_->dwarf;
    // Follow the type chain (typeref -> typedef -> base/pointer).
    for (std::size_t i = 0; i < 32 && type_offset != 0; ++i) {
        auto it = d.types.find(type_offset);
        if (it == d.types.end()) return ir::Type::Unknown;
        const auto& t = it->second;
        switch (t.kind) {
            case DwarfType::Kind::Base:
                if (t.byte_size == 1) return ir::Type::Int8;
                if (t.byte_size == 2) return ir::Type::Int16;
                if (t.byte_size == 4) return ir::Type::Int32;
                if (t.byte_size == 8) return ir::Type::Int64;
                return ir::Type::Unknown;
            case DwarfType::Kind::Pointer:
                return ir::Type::Ptr;
            case DwarfType::Kind::Typedef:
                type_offset = t.pointee_offset;
                continue;
            case DwarfType::Kind::Function:
                return ir::Type::Func;
            default:
                return ir::Type::Unknown;
        }
    }
    return ir::Type::Unknown;
}

std::string TypeInferer::function_return_type(const std::string& fn_name) const {
    if (!bin_ || !bin_->dwarf) return "void";
    for (const auto& f : bin_->dwarf->functions) {
        if (f.name == fn_name) {
            if (f.type_offset != 0) {
                const auto t = type_from_dwarf(f.type_offset);
                if (t != ir::Type::Unknown) {
                    return std::string(ir::type_c_decl(t));
                }
                // Try the DWARF type name directly.
                return bin_->dwarf->resolve_type_name(f.type_offset);
            }
            break;
        }
    }
    return "void";
}

}  // namespace nyx
