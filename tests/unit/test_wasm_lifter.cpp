// =============================================================================
// Nyx unit tests: WASM lifter (v0.2.0)
// =============================================================================
#include "nyx/parsers/wasm_lifter.hpp"
#include "nyx/parsers/wasm_parser.hpp"

#include <doctest/doctest.h>

TEST_CASE("WASM lifter: i32.add lifts to Add binop") {
    // local.get 0; local.get 1; i32.add; end
    const std::uint8_t code[] = {
        0x20, 0x00,  // local.get 0
        0x20, 0x01,  // local.get 1
        0x6A,        // i32.add
        0x0B,        // end
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};

    nyx::WasmFuncType type;
    type.params = {0x7F, 0x7F};  // two i32s
    type.results = {0x7F};

    auto fn = nyx::lift_wasm_function(body, type, "add");
    CHECK(fn.name == "add");
    CHECK_FALSE(fn.blocks.empty());

    // At least one Add instruction must have been emitted.
    bool found_add = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Add) found_add = true;
        }
    }
    CHECK(found_add);
}

TEST_CASE("WASM lifter: i32.const becomes Mov + imm") {
    // i32.const 42; end
    const std::uint8_t code[] = {
        0x41, 0x2A,  // i32.const 42
        0x0B,        // end
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;

    auto fn = nyx::lift_wasm_function(body, type, "const42");
    bool found_mov = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Mov
                && !in.operands.empty()
                && in.operands[0].kind == nyx::ir::Operand::Kind::Imm
                && in.operands[0].imm_value == 42) {
                found_mov = true;
            }
        }
    }
    CHECK(found_mov);
}

TEST_CASE("WASM lifter: local.get N maps to vreg N+1") {
    // local.get 2; end
    const std::uint8_t code[] = {
        0x20, 0x02,  // local.get 2
        0x0B,        // end
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;
    type.params = {0x7F, 0x7F, 0x7F};  // 3 params so local 2 exists

    auto fn = nyx::lift_wasm_function(body, type, "get2");
    bool found_mov_from_local3 = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Mov
                && !in.operands.empty()
                && in.operands[0].kind == nyx::ir::Operand::Kind::Register
                && in.operands[0].vreg == 3) {
                found_mov_from_local3 = true;
            }
        }
    }
    CHECK(found_mov_from_local3);
}

TEST_CASE("WASM lifter: end opcode emits Return") {
    const std::uint8_t code[] = {
        0x41, 0x01,  // i32.const 1
        0x0B,        // end
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;

    auto fn = nyx::lift_wasm_function(body, type, "end_returns");
    bool found_ret = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Return) found_ret = true;
        }
    }
    CHECK(found_ret);
}

TEST_CASE("WASM lifter: unknown opcode becomes Opaque") {
    // 0xFF is not a valid WASM opcode; the lifter should emit Opaque.
    const std::uint8_t code[] = {
        0xFF,
        0x0B,
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;

    auto fn = nyx::lift_wasm_function(body, type, "opaque");
    bool found_opaque = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Opaque) found_opaque = true;
        }
    }
    CHECK(found_opaque);
}

TEST_CASE("WASM lifter: i32.sub / i32.mul") {
    const std::uint8_t code[] = {
        0x20, 0x00, 0x20, 0x01, 0x6B,  // sub
        0x20, 0x00, 0x20, 0x01, 0x6C,  // mul
        0x0B,
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;
    type.params = {0x7F, 0x7F};

    auto fn = nyx::lift_wasm_function(body, type, "sub_mul");
    bool found_sub = false, found_mul = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Sub) found_sub = true;
            if (in.op == nyx::ir::OpCode::Mul) found_mul = true;
        }
    }
    CHECK(found_sub);
    CHECK(found_mul);
}

TEST_CASE("WASM lifter: call N lifts to Call(imm(N))") {
    const std::uint8_t code[] = {
        0x10, 0x05,  // call 5
        0x0B,        // end
    };
    nyx::WasmFuncBody body;
    body.func_idx = 0;
    body.code = nyx::ByteView{code, sizeof(code)};
    nyx::WasmFuncType type;

    auto fn = nyx::lift_wasm_function(body, type, "caller");
    bool found_call = false;
    for (const auto& b : fn.blocks) {
        for (const auto& in : b.instructions) {
            if (in.op == nyx::ir::OpCode::Call
                && !in.operands.empty()
                && in.operands[0].kind == nyx::ir::Operand::Kind::Imm
                && in.operands[0].imm_value == 5) {
                found_call = true;
            }
        }
    }
    CHECK(found_call);
}
