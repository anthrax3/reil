// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "decoder.h"

#include <cassert>

namespace reil {
namespace aarch64 {
namespace decoder {

static void PrintImmediate(std::ostream& stream, const Immediate& opnd) {
  stream << "#0x" << std::hex << opnd.value;
}

static void PrintSignedImmediate(std::ostream& stream, const Immediate& opnd) {
  if (opnd.value & 1 << (opnd.size - 1)) {
    stream << "#-0x" << std::hex << (~opnd.value) + 1;
  } else {
    stream << "#0x" << std::hex << opnd.value;
  }
}

static void PrintRegister(std::ostream& stream, const Register& opnd) {
  if (Register::kX0 <= opnd.name && opnd.name <= Register::kXzr) {
    if (opnd.size <= 32) {
      stream << "w";
    } else {
      stream << "x";
    }

    if (opnd.name == Register::kXzr) {
      stream << "zr";
    } else {
      stream << (unsigned)(opnd.name - Register::kX0);
    }
  } else if (opnd.name == Register::kSp) {
    if (opnd.size <= 32) {
      stream << "wsp";
    } else {
      stream << "sp";
    }
  } else if (opnd.name == Register::kPc) {
    stream << "pc";
  } else {
    stream << "<unsupported_reg>";
  }
}

static void PrintSystemRegister(std::ostream& stream,
                                const SystemRegister& opnd) {
  if (opnd.name == SystemRegister::kUnknown) {
    stream << "S" << std::dec << opnd.op0;
    stream << "_" << std::dec << opnd.op1;
    stream << "_C" << std::dec << opnd.crn;
    stream << "_C" << std::dec << opnd.crm;
    stream << "_" << std::dec << opnd.op2;
  } else if (opnd.name == SystemRegister::kSPSel) {
    stream << "SPSel";
  } else if (opnd.name == SystemRegister::kDAIFSet) {
    stream << "DAIFSet";
  } else if (opnd.name == SystemRegister::kDAIFClr) {
    stream << "DAIFClr";
  } else if (opnd.name == SystemRegister::kUAO) {
    stream << "UAO";
  } else if (opnd.name == SystemRegister::kPAN) {
    stream << "PAN";
  }
}

static void PrintShift(std::ostream& stream, const Shift& opnd) {
  if (opnd.type != Shift::kNone) {
    switch (opnd.type) {
      case Shift::kLsl: {
        stream << ", lsl ";
      } break;

      case Shift::kLsr: {
        stream << ", lsr ";
      } break;

      case Shift::kAsr: {
        stream << ", asr ";
      } break;

      case Shift::kRor: {
        stream << ", ror ";
      } break;

      default:
        abort();
    }

    stream << "#0x" << std::hex << (unsigned)opnd.count;
  }
}

static void PrintExtend(std::ostream& stream, const Extend& opnd) {
  if (opnd.type != Extend::kNone) {
    switch (opnd.type) {
      case Extend::kUxtb: {
        stream << ", uxtb";
      } break;

      case Extend::kUxth: {
        stream << ", uxth";
      } break;

      case Extend::kUxtw: {
        stream << ", uxtw";
      } break;

      case Extend::kUxtx: {
        stream << ", uxtx";
      } break;

      case Extend::kLsl: {
        if (opnd.count) {
          stream << ", lsl";
        }
      } break;

      case Extend::kSxtb: {
        stream << ", sxtb";
      } break;

      case Extend::kSxth: {
        stream << ", sxth";
      } break;

      case Extend::kSxtw: {
        stream << ", sxtw";
      } break;

      case Extend::kSxtx: {
        stream << ", sxtx";
      } break;

      default:
        abort();
    }

    if (opnd.count) {
      stream << ", #" << std::dec << (unsigned)opnd.count;
    }
  }
}

static void PrintImmediateOffset(std::ostream& stream,
                                 const ImmediateOffset& opnd) {
  stream << "[" << opnd.base;
  if (opnd.writeback && opnd.post_index) {
    stream << "]";
  }

  if (opnd.offset.value) {
    stream << ", ";
    PrintSignedImmediate(stream, opnd.offset);
    stream << opnd.shift;
  }

  if (!opnd.writeback || !opnd.post_index) {
    stream << "]";
    if (opnd.writeback) {
      stream << "!";
    }
  }
}

static void PrintRegisterOffset(std::ostream& stream,
                                const RegisterOffset& opnd) {
  stream << "[" << opnd.base;
  if (opnd.writeback && opnd.post_index) {
    stream << "]";
  }

  stream << ", " << opnd.offset << opnd.extend;

  if (!opnd.writeback || !opnd.post_index) {
    stream << "]";
    if (opnd.writeback) {
      stream << "!";
    }
  }
}

static void PrintOperands(std::ostream& stream,
                          const std::vector<Operand>& opnds) {
  for (size_t i = 0; i < opnds.size(); ++i) {
    if (i != 0 && !absl::holds_alternative<Shift>(opnds[i])) {
      stream << ", ";
    }
    stream << opnds[i];
  }
}

static void PrintConditionCode(std::ostream& stream, ConditionCode cc) {
  static std::vector<std::string> condition_codes = {
      "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
      "hi", "ls", "ge", "lt", "gt", "le", "al", "al"};

  stream << condition_codes[cc];
}

static void PrintPrefetchOp(std::ostream& stream, uint8_t prfop) {
  if (((prfop & 0b11000) == 0b11000) || ((prfop & 0b00110) == 0b00110)) {
    stream << "#" << std::dec << prfop;
  } else {
    switch (prfop & 0b11000) {
      case 0b00000: {
        stream << "PLD";
      } break;

      case 0b01000: {
        stream << "PLI";
      } break;

      case 0b10000: {
        stream << "PST";
      } break;

      default:
        abort();
    }

    switch (prfop & 0b00110) {
      case 0b00000: {
        stream << "L1";
      } break;

      case 0b00010: {
        stream << "L2";
      } break;

      case 0b00100: {
        stream << "L3";
      } break;

      default:
        abort();
    }

    switch (prfop & 0b1) {
      case 0: {
        stream << "KEEP";
      } break;

      case 1: {
        stream << "STRM";
      } break;
    }
  }
}

static void PrintBarrierType(std::ostream& stream, uint8_t option) {
  if (((option & 0b10) >> 1) == (option & 0b01)) {
    stream << "#" << std::dec << option;
  } else {
    switch (option >> 2) {
      case 0b00: {
        stream << "os";
      } break;

      case 0b01: {
        stream << "nsh";
      } break;

      case 0b10: {
        stream << "ish";
      } break;

      case 0b11: {
        if (option == 0b1111) {
          stream << "sy";
        }
      } break;
    }

    switch (option & 0b11) {
      case 0b01: {
        stream << "ld";
      } break;

      case 0b10: {
        stream << "st";
      } break;

      default:
        abort();
    }
  }
}

static void PrintPcRelativeAddressing(std::ostream& stream,
                                      const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Register rd = absl::get<Register>(insn.operands[0]);
  Immediate imm = absl::get<Immediate>(insn.operands[1]);
  Shift shift = absl::get<Shift>(insn.operands[2]);

  if (insn.opcode == kAdr) {
    stream << "adr ";
  } else {
    stream << "adrp ";

    assert(shift.type == Shift::kLsl);
    assert(shift.count == 12);

    imm.value <<= 12;
  }

  stream << rd << ", " << imm;
}

static void PrintAddSubtractImmediate(std::ostream& stream,
                                      const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Immediate imm = absl::get<Immediate>(insn.operands[2]);
  Shift shift = absl::get<Shift>(insn.operands[3]);

  if ((imm.value == 0 && !insn.set_flags && rd.name == Register::kSp) ||
      (imm.value == 0 && rn.name == Register::kSp)) {
    stream << "mov " << rd << ", " << rn;
  } else if (rd.name == Register::kXzr) {
    if (insn.opcode == kSubImmediate) {
      stream << "cmp ";
    } else {
      stream << "cmn ";
    }

    stream << rn << ", " << imm << shift;
  } else {
    if (insn.opcode == kSubImmediate) {
      stream << "sub";
    } else {
      stream << "add";
    }

    if (insn.set_flags) {
      stream << "s ";
    } else {
      stream << " ";
    }

    stream << rd << ", " << rn << ", " << imm << shift;
  }
}

static void PrintLogicalImmediate(std::ostream& stream,
                                  const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Immediate imm = absl::get<Immediate>(insn.operands[2]);

  switch (insn.opcode) {
    case kAndImmediate: {
      if (!insn.set_flags) {
        stream << "and ";
      } else if (rd.name == Register::kXzr) {
        stream << "tst ";
      } else {
        stream << "ands ";
      }
    } break;

    case kOrrImmediate: {
      if (rn.name == Register::kXzr) {
        stream << "mov ";
      } else {
        stream << "orr ";
      }
    } break;

    case kEorImmediate: {
      stream << "eor ";
    } break;

    default:
      abort();
  }

  if (insn.opcode != kAndImmediate || rd.name != Register::kXzr) {
    stream << rd << ", ";
  }

  if (insn.opcode != kOrrImmediate || rn.name != Register::kXzr) {
    stream << rn << ", ";
  }

  stream << imm;
}

static void PrintMoveWideImmediate(std::ostream& stream,
                                   const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Immediate imm = absl::get<Immediate>(insn.operands[1]);
  Shift shift = absl::get<Shift>(insn.operands[2]);

  switch (insn.opcode) {
    case kMovn: {
      stream << "mov ";
      imm.value <<= shift.count;
      imm.value = ~imm.value;
    } break;

    case kMovz: {
      stream << "mov ";
      imm.value <<= shift.count;
    } break;

    case kMovk: {
      stream << "movk ";
    } break;

    default:
      abort();
  }

  if (imm.size == 32) {
    imm.value &= 0xfffffffful;
  }

  stream << insn.operands[0] << ", " << imm;
  if (insn.opcode == kMovk) {
    stream << shift;
  }
}

static void PrintBitfield(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Immediate immr = absl::get<Immediate>(insn.operands[2]);
  Immediate imms = absl::get<Immediate>(insn.operands[3]);

  if (insn.opcode == kBfm) {
    if (rn.name == Register::kXzr && imms.value < immr.value) {
      stream << "bfc " << rd;
    } else if (imms.value < immr.value) {
      stream << "bfi " << rd << ", " << rn;
    } else {
      stream << "bfxil " << rd << ", " << rn;
    }

    stream << ", #" << immr.size - immr.value << ", #" << imms.value + 1;
  } else if (insn.opcode == kSbfm) {
    if ((imms.size == 32 && imms.value == 0b011111) ||
        (imms.size == 64 && imms.value == 0b111111)) {
      stream << "asr " << rd << ", " << rn << ", #" << immr.value;
    } else if (imms.value < immr.value) {
      stream << "sbfiz " << rd << ", " << rn;
      stream << ", #" << immr.size - immr.value << ", #" << imms.value + 1;
    } else if (immr.value == 0 && imms.value == 0b000111) {
      stream << "sxtb " << rd << ", " << rn;
    } else if (immr.value == 0 && imms.value == 0b001111) {
      stream << "sxth " << rd << ", " << rn;
    } else if (immr.value == 0 && imms.value == 0b011111) {
      stream << "sxtw " << rd << ", " << rn;
    } else {
      stream << "sbfx " << rd << ", " << rn;
      stream << ", #" << immr.value << ", #" << imms.value - immr.value + 1;
    }
  } else if (insn.opcode == kUbfm) {
    if ((imms.value + 1 == immr.value) &&
        (imms.value != 0b011111 && imms.value != 0b111111)) {
      stream << "lsl " << rd << ", " << rn << ", #" << immr.size - immr.value;
    } else if (imms.value == 0b011111 || imms.value == 0b111111) {
      stream << "lsr " << rd << ", " << rn << ", #" << immr.value;
    } else if (imms.value < immr.value) {
      stream << "ubfiz " << rd << ", " << rn;
      stream << ", #" << immr.size - immr.value << ", #" << imms.value + 1;
    } else if (immr.value == 0 && imms.value == 0b000111) {
      stream << "uxtb " << rd << ", " << rn;
    } else if (immr.value == 0 && imms.value == 0b001111) {
      stream << "uxth " << rd << ", " << rn;
    } else if (immr.value == 0 && imms.value == 0b011111) {
      stream << "uxtw " << rd << ", " << rn;
    } else {
      stream << "ubfx " << rd << ", " << rn;
      stream << ", #" << immr.value << ", #" << imms.value - immr.value + 1;
    }
  }
}

static void PrintExtract(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);
  Immediate imm = absl::get<Immediate>(insn.operands[3]);

  if (rn.name == rm.name) {
    stream << "ror ";
    stream << rd << ", " << rn;
  } else {
    stream << "extr ";
    stream << rd << ", " << rn << ", " << rm;
  }

  stream << ", #" << std::dec << imm.value;
}

static void PrintConditionalBranch(std::ostream& stream,
                                   const Instruction& insn) {
  assert(insn.operands.size() == 1);

  Immediate offset = absl::get<Immediate>(insn.operands[0]);
  stream << "b.";
  PrintConditionCode(stream, insn.cc);
  stream << " ";
  PrintSignedImmediate(stream, offset);
}

static void PrintExceptionGeneration(std::ostream& stream,
                                     const Instruction& insn) {
  assert(insn.operands.size() == 1);

  Immediate imm = absl::get<Immediate>(insn.operands[0]);
  switch (insn.opcode) {
    case kSvc: {
      stream << "svc #" << std::dec << imm.value;
    } break;

    case kHvc: {
      stream << "hvc #" << std::dec << imm.value;
    } break;

    case kSmc: {
      stream << "smc #" << std::dec << imm.value;
    } break;

    case kBrk: {
      stream << "brk #" << std::dec << imm.value;
    } break;

    case kHlt: {
      stream << "hlt #" << std::dec << imm.value;
    } break;

    case kDcps1: {
      stream << "dcps1";
    } break;

    case kDcps2: {
      stream << "dcps2";
    } break;

    case kDcps3: {
      stream << "dcps3";
    } break;

    default:
      abort();
  }
}

static void PrintSystem(std::ostream& stream, const Instruction& insn) {
  switch (insn.opcode) {
    case kNop: {
      stream << "nop";
    } break;

    case kYield: {
      stream << "yield";
    } break;

    case kWfe: {
      stream << "wfe";
    } break;

    case kWfi: {
      stream << "wfi";
    } break;

    case kSev: {
      stream << "sev";
    } break;

    case kSevl: {
      stream << "sevl";
    } break;

    case kXpaclri: {
      stream << "xapclri";
    } break;

    case kPacia1716: {
      stream << "pacia1716";
    } break;

    case kPacib1716: {
      stream << "pacib1716";
    } break;

    case kAutia1716: {
      stream << "autia1716";
    } break;

    case kAutib1716: {
      stream << "autib1716";
    } break;

    case kEsb: {
      stream << "esb";
    } break;

    case kPsbCsync: {
      stream << "psb csync";
    } break;

    case kPaciaz: {
      stream << "paciaz";
    } break;

    case kPaciasp: {
      stream << "paciasp";
    } break;

    case kPacibz: {
      stream << "pacibz";
    } break;

    case kPacibsp: {
      stream << "pacibsp";
    } break;

    case kAutiaz: {
      stream << "autiaz";
    } break;

    case kAutiasp: {
      stream << "autiasp";
    } break;

    case kAutibz: {
      stream << "autibz";
    } break;

    case kAutibsp: {
      stream << "autibsp";
    } break;

    case kHint: {
      stream << "hint " << insn.operands[0];
    } break;

    case kClrex: {
      stream << "clrex";
    } break;

    case kDsb: {
      Immediate imm = absl::get<Immediate>(insn.operands[0]);
      stream << "dsb ";
      PrintBarrierType(stream, imm.value);
    } break;

    case kDmb: {
      Immediate imm = absl::get<Immediate>(insn.operands[0]);
      stream << "dmb ";
      PrintBarrierType(stream, imm.value);
    } break;

    case kIsb: {
      Immediate imm = absl::get<Immediate>(insn.operands[0]);
      stream << "isb";
      if (imm.value != 0b1111) {
        stream << " #" << std::dec << imm.value;
      }
    } break;

    case kSys: {
      // TODO: handle AT etc.
      Immediate op1 = absl::get<Immediate>(insn.operands[0]);
      Immediate crn = absl::get<Immediate>(insn.operands[1]);
      Immediate crm = absl::get<Immediate>(insn.operands[2]);
      Immediate op2 = absl::get<Immediate>(insn.operands[3]);
      Register rt = absl::get<Register>(insn.operands[4]);

      stream << "sys ";
      stream << "#" << std::dec << op1.value;
      stream << ", C" << std::dec << crn.value;
      stream << ", C" << std::dec << crm.value;
      stream << ", #" << std::dec << op2.value;
      if (rt.name != Register::kXzr) {
        stream << ", " << rt;
      }
    } break;

    case kMsr: {
      stream << "msr ";
      PrintOperands(stream, insn.operands);
    } break;

    case kSysl: {
      Register rt = absl::get<Register>(insn.operands[0]);
      Immediate op1 = absl::get<Immediate>(insn.operands[1]);
      Immediate crn = absl::get<Immediate>(insn.operands[2]);
      Immediate crm = absl::get<Immediate>(insn.operands[3]);
      Immediate op2 = absl::get<Immediate>(insn.operands[4]);

      stream << "sysl ";
      stream << rt;
      stream << ", #" << std::dec << op1.value;
      stream << ", C" << std::dec << crn.value;
      stream << ", C" << std::dec << crm.value;
      stream << ", #" << std::dec << op2.value;
    } break;

    case kMrs: {
      stream << "mrs ";
      PrintOperands(stream, insn.operands);
    } break;

    default:
      abort();
  }
}

static void PrintBranchRegister(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() >= 1);

  Register rn = absl::get<Register>(insn.operands[0]);

  switch (insn.opcode) {
    case kBr: {
      stream << "br " << rn;
    } break;

    case kBraaz: {
      stream << "braaz " << rn;
    } break;

    case kBrabz: {
      stream << "brabz" << rn;
    } break;

    case kBlr: {
      stream << "blr " << rn;
    } break;

    case kBlraaz: {
      stream << "blraaz " << rn;
    } break;

    case kBlrabz: {
      stream << "blrabz " << rn;
    } break;

    case kRet: {
      stream << "ret";
      if (rn.name != Register::kX30) {
        stream << " " << rn;
      }
    } break;

    case kRetaa: {
      stream << "retaa";
      if (rn.name != Register::kX30) {
        stream << " " << rn;
      }
    } break;

    case kRetab: {
      stream << "retab";
      if (rn.name != Register::kX30) {
        stream << " " << rn;
      }
    } break;

    case kEret: {
      stream << "eret";
    } break;

    case kEretaa: {
      stream << "eretaa";
    } break;

    case kEretab: {
      stream << "eretab";
    } break;

    case kDrps: {
      stream << "drps";
    } break;

    case kBraa: {
      stream << "braa " << rn << ", " << insn.operands[1];
    } break;

    case kBrab: {
      stream << "brab " << rn << ", " << insn.operands[1];
    } break;

    case kBlraa: {
      stream << "blraa " << rn << ", " << insn.operands[1];
    } break;

    case kBlrab: {
      stream << "blrab " << rn << ", " << insn.operands[1];
    } break;

    default:
      abort();
  }
}

static void PrintBranchImmediate(std::ostream& stream,
                                 const Instruction& insn) {
  assert(insn.operands.size() == 1);

  Immediate offset = absl::get<Immediate>(insn.operands[0]);

  if (insn.opcode == kBl) {
    stream << "bl ";
  } else {
    stream << "b ";
  }

  PrintSignedImmediate(stream, offset);
}

static void PrintCompareAndBranch(std::ostream& stream,
                                  const Instruction& insn) {
  assert(insn.operands.size() == 2);

  Immediate offset = absl::get<Immediate>(insn.operands[1]);

  if (insn.opcode == kCbz) {
    stream << "cbz ";
  } else {
    stream << "cbnz ";
  }

  stream << insn.operands[0] << ", ";
  PrintSignedImmediate(stream, offset);
}

static void PrintTestAndBranch(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Immediate bit = absl::get<Immediate>(insn.operands[1]);
  Immediate offset = absl::get<Immediate>(insn.operands[2]);

  if (insn.opcode == kTbz) {
    stream << "tbz ";
  } else {
    stream << "tbnz ";
  }

  stream << insn.operands[0] << ", #" << std::dec << bit.value << ", ";
  PrintSignedImmediate(stream, offset);
}

static void PrintLoadStoreExclusive(std::ostream& stream,
                                    const Instruction& insn) {
  bool pair = false;
  uint8_t size = 64;

  if (insn.opcode <= kLdxr) {
    if (insn.opcode == kLdxr) {
      stream << "ldxr";
    } else if (insn.opcode == kLdxp) {
      stream << "ldxp ";
      pair = true;
    } else if (insn.opcode == kLdaxr) {
      stream << "ldaxr";
    } else if (insn.opcode == kLdaxp) {
      stream << "ldaxp ";
      pair = true;
    } else if (insn.opcode == kLdlar) {
      stream << "ldlar";
    } else {
      stream << "ldar";
    }

    size = absl::get<Register>(insn.operands[0]).size;
  } else if (insn.opcode <= kStxr) {
    if (insn.opcode == kStxr) {
      stream << "stxr";
    } else if (insn.opcode == kStxp) {
      stream << "stxp ";
      pair = true;
    } else if (insn.opcode == kStlxr) {
      stream << "stlxr";
    } else if (insn.opcode == kStlxp) {
      stream << "stlxp ";
      pair = true;
    } else if (insn.opcode == kStllr) {
      stream << "stllr";
    } else if (insn.opcode == kStlr) {
      stream << "stlr";
    }

    if (insn.opcode != kStlr && insn.opcode != kStllr) {
      size = absl::get<Register>(insn.operands[1]).size;
    } else {
      size = absl::get<Register>(insn.operands[0]).size;
    }
  }

  if (!pair) {
    if (size == 8) {
      stream << "b ";
    } else if (size == 16) {
      stream << "h ";
    } else {
      stream << " ";
    }
  }

  PrintOperands(stream, insn.operands);
}

static void PrintLoadLiteral(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 2);

  ImmediateOffset imm_off = absl::get<ImmediateOffset>(insn.operands[1]);

  if (insn.opcode == kLdrLiteral) {
    stream << "ldr ";
  } else if (insn.opcode == kLdrsLiteral) {
    stream << "ldrsw ";
  } else if (insn.opcode == kPrfmLiteral) {
    stream << "prfm ";
  } else {
    abort();
  }

  if (insn.opcode == kPrfmLiteral) {
    Immediate prfop = absl::get<Immediate>(insn.operands[0]);
    PrintPrefetchOp(stream, prfop.value);
  } else {
    stream << insn.operands[0] << ", ";
  }
  PrintSignedImmediate(stream, imm_off.offset);
}

static void PrintLoadStorePair(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 3);

  if (insn.opcode == kLdp) {
    stream << "ldp ";
  } else if (insn.opcode == kLdpsw) {
    stream << "ldpsw ";
  } else if (insn.opcode == kLdnp) {
    stream << "ldnp ";
  } else if (insn.opcode == kStp) {
    stream << "stp ";
  } else if (insn.opcode == kStnp) {
    stream << "stnp ";
  } else {
    abort();
  }

  PrintOperands(stream, insn.operands);
}

static void PrintLoadStore(std::ostream& stream, const Instruction& insn) {
  assert(insn.operands.size() == 2);
  uint8_t size = 0;

  if (absl::holds_alternative<ImmediateOffset>(insn.operands[1])) {
    ImmediateOffset address = absl::get<ImmediateOffset>(insn.operands[1]);
    size = address.size;
  } else if (absl::holds_alternative<RegisterOffset>(insn.operands[1])) {
    RegisterOffset address = absl::get<RegisterOffset>(insn.operands[1]);
    size = address.size;
  } else {
    abort();
  }

  if (insn.opcode == kPrfm) {
    Immediate prfop = absl::get<Immediate>(insn.operands[0]);
    stream << "prfm ";
    PrintPrefetchOp(stream, prfop.value);
    stream << ", " << insn.operands[1];
  } else {
    if (insn.opcode == kLdr) {
      stream << "ldr";
    } else if (insn.opcode == kLdur) {
      stream << "ldur";
    } else if (insn.opcode == kLdtr) {
      stream << "ldtr";
    } else if (insn.opcode == kLdrs) {
      stream << "ldrs";
    } else if (insn.opcode == kLdurs) {
      stream << "ldurs";
    } else if (insn.opcode == kLdtrs) {
      stream << "ldtrs";
    } else if (insn.opcode == kStr) {
      stream << "str";
    } else if (insn.opcode == kStur) {
      stream << "stur";
    } else if (insn.opcode == kSttr) {
      stream << "sttr";
    } else {
      abort();
    }

    if (size == 8) {
      stream << "b ";
    } else if (size == 16) {
      stream << "h ";
    } else if (size == 32 && (insn.opcode == kLdrs || insn.opcode == kLdurs ||
                              insn.opcode == kLdtrs)) {
      stream << "w ";
    } else {
      stream << " ";
    }

    PrintOperands(stream, insn.operands);
  }
}

static void PrintDataProcessingTwoSource(std::ostream& stream,
                                         const Instruction& insn) {
  assert(insn.operands.size() == 3);

  switch (insn.opcode) {
    case kAsr: {
      stream << "asr ";
    } break;

    case kLsl: {
      stream << "lsl ";
    } break;

    case kLsr: {
      stream << "lsr ";
    } break;

    case kRor: {
      stream << "ror ";
    } break;

    case kSdiv: {
      stream << "sdiv ";
    } break;

    case kUdiv: {
      stream << "udiv ";
    } break;

    case kPacga: {
      stream << "pacga ";
    } break;

    case kCrc32b: {
      stream << "crc32b ";
    } break;

    case kCrc32h: {
      stream << "crc32h ";
    } break;

    case kCrc32w: {
      stream << "crc32w ";
    } break;

    case kCrc32x: {
      stream << "crc32x ";
    } break;

    case kCrc32cb: {
      stream << "crc32cb ";
    } break;

    case kCrc32ch: {
      stream << "crc32ch ";
    } break;

    case kCrc32cw: {
      stream << "crc32cw ";
    } break;

    case kCrc32cx: {
      stream << "crc32cx ";
    } break;

    default:
      abort();
  }

  PrintOperands(stream, insn.operands);
}

static void PrintDataProcessingOneSource(std::ostream& stream,
                                         const Instruction& insn) {
  assert(insn.operands.size() == 2);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);

  switch (insn.opcode) {
    case kRbit: {
      stream << "rbit " << rd << ", " << rn;
    } break;

    case kRev16: {
      stream << "rev16 " << rd << ", " << rn;
    } break;

    case kRev32: {
      stream << "rev32 " << rd << ", " << rn;
    } break;

    case kRev: {
      stream << "rev " << rd << ", " << rn;
    } break;

    case kClz: {
      stream << "clz " << rd << ", " << rn;
    } break;

    case kCls: {
      stream << "cls " << rd << ", " << rn;
    } break;

    case kPacia: {
      if (rn.name == Register::kXzr) {
        stream << "paciza " << rd;
      } else {
        stream << "pacia " << rd << ", " << rn;
      }
    } break;

    case kPacib: {
      if (rn.name == Register::kXzr) {
        stream << "pacizb " << rd;
      } else {
        stream << "pacib " << rd << ", " << rn;
      }
    } break;

    case kPacda: {
      if (rn.name == Register::kXzr) {
        stream << "pacdza " << rd;
      } else {
        stream << "pacda " << rd << ", " << rn;
      }
    } break;

    case kPacdb: {
      if (rn.name == Register::kXzr) {
        stream << "pacdzb " << rd;
      } else {
        stream << "pacdb " << rd << ", " << rn;
      }
    } break;

    case kAutia: {
      if (rn.name == Register::kXzr) {
        stream << "autiza " << rd;
      } else {
        stream << "autia " << rd << ", " << rn;
      }
    } break;

    case kAutib: {
      if (rn.name == Register::kXzr) {
        stream << "autizb " << rd;
      } else {
        stream << "autib " << rd << ", " << rn;
      }
    } break;

    case kAutda: {
      if (rn.name == Register::kXzr) {
        stream << "autdza " << rd;
      } else {
        stream << "autda " << rd << ", " << rn;
      }
    } break;

    case kAutdb: {
      if (rn.name == Register::kXzr) {
        stream << "autdzb " << rd;
      } else {
        stream << "autda " << rd << ", " << rn;
      }
    } break;

    case kXpaci: {
      stream << "xpaci " << rd;
    } break;

    case kXpacd: {
      stream << "xpacd " << rd;
    } break;

    default:
      abort();
  }
}

static void PrintLogicalShiftedRegister(std::ostream& stream,
                                        const Instruction& insn) {
  assert(insn.operands.size() == 4);

  switch (insn.opcode) {
    case kAndShiftedRegister: {
      if (insn.set_flags) {
        stream << "ands ";
      } else {
        stream << "and ";
      }
    } break;

    case kBicShiftedRegister: {
      if (insn.set_flags) {
        stream << "bics ";
      } else {
        stream << "bic ";
      }
    } break;

    case kOrrShiftedRegister: {
      stream << "orr ";
    } break;

    case kOrnShiftedRegister: {
      stream << "orn ";
    } break;

    case kEorShiftedRegister: {
      stream << "eor ";
    } break;

    case kEonShiftedRegister: {
      stream << "eon ";
    } break;

    default:
      abort();
  }

  PrintOperands(stream, insn.operands);
}

static void PrintAddSubtractShiftedRegister(std::ostream& stream,
                                            const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);
  Shift shift = absl::get<Shift>(insn.operands[3]);

  if (insn.opcode == kSubShiftedRegister) {
    if (insn.set_flags) {
      if (rd.name == Register::kXzr) {
        stream << "cmp " << rn << ", " << rm << shift;
      } else if (rn.name == Register::kXzr) {
        stream << "negs " << rd << ", " << rm << shift;
      } else {
        stream << "subs " << rd << ", " << rn << ", " << rm << shift;
      }
    } else {
      if (rn.name == Register::kXzr) {
        stream << "neg " << rd << ", " << rm << shift;
      } else {
        stream << "sub " << rd << ", " << rn << ", " << rm << shift;
      }
    }
  } else {
    if (insn.set_flags) {
      if (rd.name == Register::kXzr) {
        stream << "cmn " << rn << ", " << rm << shift;
      } else {
        stream << "adds " << rd << ", " << rn << ", " << rm << shift;
      }
    } else {
      stream << "add " << rd << ", " << rn << ", " << rm << shift;
    }
  }
}

static void PrintAddSubtractExtendedRegister(std::ostream& stream,
                                             const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);
  Extend extend = absl::get<Extend>(insn.operands[3]);

  if (insn.opcode == kSubExtendedRegister) {
    if (insn.set_flags) {
      if (rd.name == Register::kXzr) {
        stream << "cmp " << rn << ", " << rm << extend;
      } else {
        stream << "subs " << rd << ", " << rn << ", " << rm << extend;
      }
    } else {
      stream << "sub " << rd << ", " << rn << ", " << rm << extend;
    }
  } else {
    if (insn.set_flags) {
      if (rd.name == Register::kXzr) {
        stream << "cmn " << rn << ", " << rm << extend;
      } else {
        stream << "adds " << rd << ", " << rn << ", " << rm << extend;
      }
    } else {
      stream << "add " << rd << ", " << rn << ", " << rm << extend;
    }
  }
}

static void PrintAddSubtractWithCarry(std::ostream& stream,
                                      const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);

  if (insn.opcode == kSbc) {
    if (insn.set_flags) {
      if (rn.name == Register::kXzr) {
        stream << "ngcs " << rd << ", " << rm;
      } else {
        stream << "sbcs " << rd << ", " << rn << ", " << rm;
      }
    } else {
      if (rn.name == Register::kXzr) {
        stream << "ngc " << rd << ", " << rm;
      } else {
        stream << "sbc " << rd << ", " << rn << ", " << rm;
      }
    }
  } else {
    if (insn.set_flags) {
      stream << "adcs " << rd << ", " << rn << ", " << rm;
    } else {
      stream << "adc " << rd << ", " << rn << ", " << rm;
    }
  }
}

static void PrintConditionalCompare(std::ostream& stream,
                                    const Instruction& insn) {
  assert(insn.operands.size() == 3);

  if (insn.opcode == kCcmn) {
    stream << "ccmn ";
  } else {
    stream << "ccmp ";
  }

  PrintOperands(stream, insn.operands);
  stream << ", ";
  PrintConditionCode(stream, insn.cc);
}

static void PrintConditionalSelect(std::ostream& stream,
                                   const Instruction& insn) {
  assert(insn.operands.size() == 3);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);

  if (insn.opcode == kCsel) {
    stream << "csel " << rd << ", " << rn << ", " << rm;
  } else if (insn.opcode == kCsinc) {
    if (rn.name == Register::kXzr && rm.name == Register::kXzr) {
      stream << "cset " << rd;
    } else if (rn.name == rm.name) {
      stream << "cinc " << rd << ", " << rn;
    } else {
      stream << "csinc " << rd << ", " << rn << ", " << rm;
    }
  } else if (insn.opcode == kCsinv) {
    if (rn.name == Register::kXzr && rm.name == Register::kXzr) {
      stream << "csetm " << rd;
    } else if (rn.name == rm.name) {
      stream << "cinv " << rd << ", " << rn;
    } else {
      stream << "csinv " << rd << ", " << rn << ", " << rm;
    }
  } else if (insn.opcode == kCsneg) {
    if (rn.name == rm.name) {
      stream << "cneg " << rd << ", " << rn;
    } else {
      stream << "csneg " << rd << ", " << rn << ", " << rm;
    }
  } else {
    abort();
  }

  stream << ", ";
  PrintConditionCode(stream, insn.cc);
}

static void PrintDataProcessingThreeSource(std::ostream& stream,
                                           const Instruction& insn) {
  assert(insn.operands.size() == 4);

  Register rd = absl::get<Register>(insn.operands[0]);
  Register rn = absl::get<Register>(insn.operands[1]);
  Register rm = absl::get<Register>(insn.operands[2]);
  Register ra = absl::get<Register>(insn.operands[3]);

  switch (insn.opcode) {
    case kMadd: {
      if (ra.name == Register::kXzr) {
        stream << "mul " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "madd " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kMsub: {
      if (ra.name == Register::kXzr) {
        stream << "mneg " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "msub " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kSmaddl: {
      if (ra.name == Register::kXzr) {
        stream << "smull " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "smaddl " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kSmsubl: {
      if (ra.name == Register::kXzr) {
        stream << "smnegl " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "smsubl " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kSmulh: {
      stream << "smulh " << rd << ", " << rn << ", " << rm;
    } break;

    case kUmaddl: {
      if (ra.name == Register::kXzr) {
        stream << "umull " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "umaddl " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kUmsubl: {
      if (ra.name == Register::kXzr) {
        stream << "umnegl " << rd << ", " << rn << ", " << rm;
      } else {
        stream << "umsubl " << rd << ", " << rn << ", " << rm << ", " << ra;
      }
    } break;

    case kUmulh: {
      stream << "umulh " << rd << ", " << rn << ", " << rm;
    } break;

    default:
      abort();
  }
}

std::ostream& operator<<(std::ostream& stream, Instruction insn) {
  if (insn.opcode <= kAdrp) {
    PrintPcRelativeAddressing(stream, insn);
  } else if (insn.opcode <= kSubImmediate) {
    PrintAddSubtractImmediate(stream, insn);
  } else if (insn.opcode <= kEorImmediate) {
    PrintLogicalImmediate(stream, insn);
  } else if (insn.opcode <= kMovz) {
    PrintMoveWideImmediate(stream, insn);
  } else if (insn.opcode <= kUbfm) {
    PrintBitfield(stream, insn);
  } else if (insn.opcode <= kExtr) {
    PrintExtract(stream, insn);
  } else if (insn.opcode <= kBCond) {
    PrintConditionalBranch(stream, insn);
  } else if (insn.opcode <= kSvc) {
    PrintExceptionGeneration(stream, insn);
  } else if (insn.opcode <= kYield) {
    PrintSystem(stream, insn);
  } else if (insn.opcode <= kRetabz) {
    PrintBranchRegister(stream, insn);
  } else if (insn.opcode <= kBl) {
    PrintBranchImmediate(stream, insn);
  } else if (insn.opcode <= kCbz) {
    PrintCompareAndBranch(stream, insn);
  } else if (insn.opcode <= kTbz) {
    PrintTestAndBranch(stream, insn);
  } else if (insn.opcode <= kStxr) {
    PrintLoadStoreExclusive(stream, insn);
  } else if (insn.opcode <= kPrfmLiteral) {
    PrintLoadLiteral(stream, insn);
  } else if (insn.opcode <= kStp) {
    PrintLoadStorePair(stream, insn);
  } else if (insn.opcode <= kStur) {
    PrintLoadStore(stream, insn);
  } else if (insn.opcode <= kUdiv) {
    PrintDataProcessingTwoSource(stream, insn);
  } else if (insn.opcode <= kXpaci) {
    PrintDataProcessingOneSource(stream, insn);
  } else if (insn.opcode <= kEonShiftedRegister) {
    PrintLogicalShiftedRegister(stream, insn);
  } else if (insn.opcode <= kSubShiftedRegister) {
    PrintAddSubtractShiftedRegister(stream, insn);
  } else if (insn.opcode <= kSubExtendedRegister) {
    PrintAddSubtractExtendedRegister(stream, insn);
  } else if (insn.opcode <= kSbc) {
    PrintAddSubtractWithCarry(stream, insn);
  } else if (insn.opcode <= kCcmp) {
    PrintConditionalCompare(stream, insn);
  } else if (insn.opcode <= kCsneg) {
    PrintConditionalSelect(stream, insn);
  } else if (insn.opcode <= kUmsubl) {
    PrintDataProcessingThreeSource(stream, insn);
  } else {
    stream << "<unsupported_insn>";
  }

  return stream;
}

std::ostream& operator<<(std::ostream& stream, Operand operand) {
  switch (operand.index()) {
    case kImmediate: {
      PrintImmediate(stream, absl::get<Immediate>(operand));
    } break;

    case kRegister: {
      PrintRegister(stream, absl::get<Register>(operand));
    } break;

    case kSystemRegister: {
      PrintSystemRegister(stream, absl::get<SystemRegister>(operand));
    } break;

    case kShift: {
      PrintShift(stream, absl::get<Shift>(operand));
    } break;

    case kExtend: {
      PrintExtend(stream, absl::get<Extend>(operand));
    } break;

    case kImmediateOffset: {
      PrintImmediateOffset(stream, absl::get<ImmediateOffset>(operand));
    } break;

    case kRegisterOffset: {
      PrintRegisterOffset(stream, absl::get<RegisterOffset>(operand));
    } break;

    default:
      stream << "<unsupported_opnd>";
  }

  return stream;
}
}  // namespace decoder
}  // namespace aarch64
}  // namespace reil
