// =============================================================================
// Nyx unit tests: TypeInferer (v0.0.4)
// =============================================================================
#include "nyx/decompiler/type_inferer.hpp"

#include <doctest/doctest.h>

TEST_CASE("TypeInferer: type_from_size maps byte sizes to IR types") {
    CHECK(nyx::TypeInferer::type_from_size(0) == nyx::ir::Type::Unknown);
    CHECK(nyx::TypeInferer::type_from_size(1) == nyx::ir::Type::Int8);
    CHECK(nyx::TypeInferer::type_from_size(2) == nyx::ir::Type::Int16);
    CHECK(nyx::TypeInferer::type_from_size(4) == nyx::ir::Type::Int32);
    CHECK(nyx::TypeInferer::type_from_size(8) == nyx::ir::Type::Int64);
    CHECK(nyx::TypeInferer::type_from_size(16) == nyx::ir::Type::Unknown);
}

TEST_CASE("TypeInferer: type_c_decl returns valid C declarations") {
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Unknown) == "void*");
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Int8) == "char");
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Int16) == "short");
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Int32) == "int");
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Int64) == "long long");
    CHECK(nyx::ir::type_c_decl(nyx::ir::Type::Ptr) == "void*");
}

TEST_CASE("TypeInferer: type_size respects arch bitness") {
    CHECK(nyx::ir::type_size(nyx::ir::Type::Int32, 32) == 4);
    CHECK(nyx::ir::type_size(nyx::ir::Type::Int32, 64) == 4);
    CHECK(nyx::ir::type_size(nyx::ir::Type::Ptr, 32) == 4);
    CHECK(nyx::ir::type_size(nyx::ir::Type::Ptr, 64) == 8);
    CHECK(nyx::ir::type_size(nyx::ir::Type::Ptr, 0) == 0);
}

TEST_CASE("TypeInferer: infers Int32 for small immediate mov") {
    // Build a tiny IR function: v1 = 0x10
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(0x10));
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    nyx::TypeInferer inferer(nyx::Arch::X86_64, nullptr);
    inferer.infer(fn);

    CHECK(inferer.type_of(fn, 1) == nyx::ir::Type::Int32);
}

TEST_CASE("TypeInferer: infers Int64 for large immediate mov") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.mov(1, nyx::ir::Operand::imm(0x100000000LL));  // > 32 bits
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    nyx::TypeInferer inferer(nyx::Arch::X86_64, nullptr);
    inferer.infer(fn);

    CHECK(inferer.type_of(fn, 1) == nyx::ir::Type::Int64);
}

TEST_CASE("TypeInferer: Cmp result is Int32") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.cmp(1, nyx::ir::Operand::reg(2), nyx::ir::Operand::imm(0));
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    nyx::TypeInferer inferer(nyx::Arch::X86_64, nullptr);
    inferer.infer(fn);

    CHECK(inferer.type_of(fn, 1) == nyx::ir::Type::Int32);
}

TEST_CASE("TypeInferer: binop inherits wider operand type") {
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    // v1 = 0x10  (Int32)
    // v2 = 0x100000000LL  (Int64)
    // v3 = v1 + v2  (should be Int64)
    b.mov(1, nyx::ir::Operand::imm(0x10));
    b.mov(2, nyx::ir::Operand::imm(0x100000000LL));
    b.binop(nyx::ir::OpCode::Add, 3, nyx::ir::Operand::reg(1), nyx::ir::Operand::reg(2));
    insns = std::move(b).finish();
    insns[0].addr = 0x1000;
    insns[1].addr = 0x1001;
    insns[2].addr = 0x1002;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x1000, "f");
    nyx::TypeInferer inferer(nyx::Arch::X86_64, nullptr);
    inferer.infer(fn);

    CHECK(inferer.type_of(fn, 1) == nyx::ir::Type::Int32);
    CHECK(inferer.type_of(fn, 2) == nyx::ir::Type::Int64);
    CHECK(inferer.type_of(fn, 3) == nyx::ir::Type::Int64);
}

TEST_CASE("TypeInferer: type_from_symbol via BinaryInfo") {
    nyx::BinaryInfo bin;
    nyx::Symbol s;
    s.name = "g_counter";
    s.value = 0x401000;
    s.size = 4;
    s.kind = nyx::Symbol::Kind::Object;
    bin.symbols.push_back(s);

    nyx::TypeInferer inferer(nyx::Arch::X86_64, &bin);
    // The inferer's type_from_symbol is private; we verify the behavior
    // indirectly by checking that a Load with a matching displacement
    // infers the symbol's type.
    std::vector<nyx::ir::Instruction> insns;
    nyx::ir::Builder b;
    b.load(1, nyx::ir::Operand::mem(nyx::ir::INVALID_VREG, 0x401000, 0));
    insns = std::move(b).finish();
    insns[0].addr = 0x2000;

    auto fn = nyx::ir::CFGBuilder::build(std::move(insns), 0x2000, "f");
    inferer.infer(fn);

    // The load's displacement matches g_counter's address (size 4 => Int32).
    CHECK(inferer.type_of(fn, 1) == nyx::ir::Type::Int32);
}
