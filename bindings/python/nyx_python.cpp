// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// Python bindings for Nyx via pybind11.
//
// Exposes: BinaryInfo, Decompiler, and a convenience decompile_file()
// function that returns a string (for text/pseudo-c/dot) or a dict-like
// structure (for JSON).
// =============================================================================
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/lifter/cfg_analysis.hpp"
#include "nyx/output/json_writer.hpp"
#include "nyx/output/pseudo_c_writer.hpp"
#include "nyx/output/text_writer.hpp"
#include "nyx/output/dot_writer.hpp"
#include "nyx/output/annotated_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <sstream>
#include <stdexcept>

namespace py = pybind11;

PYBIND11_MODULE(nyx_python, m) {
    m.doc() = "Nyx - a headless decompilation engine (Python bindings)";

    // Arch enum
    py::enum_<nyx::Arch>(m, "Arch")
        .value("Unknown", nyx::Arch::Unknown)
        .value("X86", nyx::Arch::X86)
        .value("X86_64", nyx::Arch::X86_64)
        .value("ARM", nyx::Arch::ARM)
        .value("AARCH64", nyx::Arch::AARCH64)
        .value("PPC", nyx::Arch::PPC)
        .value("PPC64", nyx::Arch::PPC64)
        .value("MIPS", nyx::Arch::MIPS)
        .value("MIPS64", nyx::Arch::MIPS64);

    // BinaryFormat enum
    py::enum_<nyx::BinaryFormat>(m, "BinaryFormat")
        .value("Unknown", nyx::BinaryFormat::Unknown)
        .value("Elf", nyx::BinaryFormat::Elf)
        .value("Pe", nyx::BinaryFormat::Pe)
        .value("MachO", nyx::BinaryFormat::MachO);

    // Section
    py::class_<nyx::Section>(m, "Section")
        .def_readonly("name", &nyx::Section::name)
        .def_readonly("vaddr", &nyx::Section::vaddr)
        .def_readonly("file_off", &nyx::Section::file_off)
        .def_readonly("file_size", &nyx::Section::file_size)
        .def_readonly("executable", &nyx::Section::executable)
        .def_readonly("is_code", &nyx::Section::is_code);

    // Symbol
    py::class_<nyx::Symbol>(m, "Symbol")
        .def_readonly("name", &nyx::Symbol::name)
        .def_readonly("value", &nyx::Symbol::value)
        .def_readonly("size", &nyx::Symbol::size)
        .def_readonly("imported", &nyx::Symbol::imported)
        .def_readonly("exported", &nyx::Symbol::exported);

    // BinaryInfo
    py::class_<nyx::BinaryInfo>(m, "BinaryInfo")
        .def_readonly("path", &nyx::BinaryInfo::path)
        .def_readonly("format", &nyx::BinaryInfo::format)
        .def_readonly("arch", &nyx::BinaryInfo::arch)
        .def_readonly("is_64bit", &nyx::BinaryInfo::is_64bit)
        .def_readonly("is_pie", &nyx::BinaryInfo::is_pie)
        .def_readonly("has_nx", &nyx::BinaryInfo::has_nx)
        .def_readonly("stripped", &nyx::BinaryInfo::stripped)
        .def_readonly("entry_point", &nyx::BinaryInfo::entry_point)
        .def_readonly("image_base", &nyx::BinaryInfo::image_base)
        .def_readonly("sections", &nyx::BinaryInfo::sections)
        .def_readonly("symbols", &nyx::BinaryInfo::symbols)
        .def("find_section", [](const nyx::BinaryInfo& self, const std::string& name) -> py::object {
            const auto* s = self.find_section(name);
            if (s) return py::cast(*s);
            return py::none();
        })
        .def("find_symbol", [](const nyx::BinaryInfo& self, const std::string& name) -> py::object {
            const auto* s = self.find_symbol(name);
            if (s) return py::cast(*s);
            return py::none();
        });

    // DecompiledFunction
    py::class_<nyx::DecompiledFunction>(m, "DecompiledFunction")
        .def_readonly("name", &nyx::DecompiledFunction::name)
        .def_readonly("entry", &nyx::DecompiledFunction::entry)
        .def_readonly("block_count", &nyx::DecompiledFunction::block_count)
        .def_readonly("insn_count", &nyx::DecompiledFunction::insn_count)
        .def_readonly("lines", &nyx::DecompiledFunction::lines)
        .def("__str__", [](const nyx::DecompiledFunction& self) {
            std::string s;
            for (const auto& l : self.lines) { s += l; s += "\n"; }
            return s;
        });

    // Convenience: load_and_parse
    m.def("load", [](const std::string& path) {
        return nyx::BinaryParser::load_and_parse(path);
    }, py::arg("path"), "Load and parse a binary file.");

    // Convenience: decompile_file
    m.def("decompile_file", [](const std::string& path, const std::string& format) -> py::object {
        nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(path);

        if (format == "json") {
            nyx::Decompiler dec;
            auto funcs = dec.decompile(bin);
            std::ostringstream os;
            nyx::output::write_json(os, bin, funcs);
            return py::str(os.str());
        }

        if (format == "text") {
            nyx::Disassembler dis(bin.arch, bin.endian);
            auto buf = nyx::ByteBuffer::from_file(path);
            std::vector<std::vector<nyx::DecodedInstruction>> sections;
            if (buf) {
                for (const auto& s : bin.sections) {
                    if (!s.executable || s.is_nobits) continue;
                    if (s.file_off >= buf->size()) continue;
                    const std::size_t avail = buf->size() - s.file_off;
                    const std::size_t n = std::min<std::size_t>(s.file_size, avail);
                    if (n == 0) continue;
                    nyx::ByteView bytes{buf->data() + s.file_off, n};
                    sections.push_back(dis.decode(bytes, s.vaddr));
                }
            }
            std::ostringstream os;
            nyx::output::write_text(os, bin, sections);
            return py::str(os.str());
        }

        if (format == "pseudo-c" || format == "pseudoc" || format == "c") {
            nyx::Decompiler dec;
            auto funcs = dec.decompile(bin);
            std::ostringstream os;
            nyx::output::write_pseudo_c(os, bin, funcs);
            return py::str(os.str());
        }

        if (format == "annotated") {
            nyx::Disassembler dis(bin.arch, bin.endian);
            auto buf = nyx::ByteBuffer::from_file(path);
            std::vector<std::vector<nyx::DecodedInstruction>> sections;
            if (buf) {
                for (const auto& s : bin.sections) {
                    if (!s.executable || s.is_nobits) continue;
                    if (s.file_off >= buf->size()) continue;
                    const std::size_t avail = buf->size() - s.file_off;
                    const std::size_t n = std::min<std::size_t>(s.file_size, avail);
                    if (n == 0) continue;
                    nyx::ByteView bytes{buf->data() + s.file_off, n};
                    sections.push_back(dis.decode(bytes, s.vaddr));
                }
            }
            std::ostringstream os;
            nyx::output::write_annotated(os, bin, sections);
            return py::str(os.str());
        }

        if (format == "dot") {
            nyx::Decompiler dec;
            auto ir_fns = dec.decompile_ir(bin);
            std::ostringstream os;
            for (const auto& fn : ir_fns) {
                auto dom = nyx::ir::compute_dominators(fn);
                auto loops = nyx::ir::find_natural_loops(fn, dom);
                nyx::output::write_dot(os, fn, dom, loops);
            }
            return py::str(os.str());
        }

        throw std::runtime_error("unknown format: " + format);
    }, py::arg("path"), py::arg("format") = "pseudo-c",
       "Decompile a binary file and return the output as a string.");

    // Version
    m.attr("__version__") = "0.1.0";
}
