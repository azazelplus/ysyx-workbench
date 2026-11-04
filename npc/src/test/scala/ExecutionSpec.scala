package azazerv

import chisel3._
import chisel3.util._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import azazerv.idu.IDU
import azazerv.exu.EXU

/** Exercise instruction decode and execution together, with downstream backpressure. */
class DecodeExecuteHarness(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new IF2ID))
    val rs1 = Input(UInt(32.W))
    val rs2 = Input(UInt(32.W))
    val csr_rdata = Input(UInt(32.W))
    val out = Decoupled(new EX2LS)
    val jump_en = Output(Bool())
    val jump_addr = Output(UInt(32.W))
    val csr_wen = Output(Bool())
    val csr_wdata = Output(UInt(32.W))
    val is_ecall = Output(Bool())
    val is_mret = Output(Bool())
  })
  val idu = Module(new IDU)
  val exu = Module(new EXU)
  idu.io.in <> io.in
  idu.io.rs1_data := io.rs1
  idu.io.rs2_data := io.rs2
  exu.io.in <> idu.io.out
  io.out <> exu.io.out
  exu.io.csr_rdata := io.csr_rdata
  exu.io.mtvec := "h80001000".U
  exu.io.mepc := "h80002000".U
  io.jump_en := exu.io.jump_en
  io.jump_addr := exu.io.jump_addr
  io.csr_wen := exu.io.csr_wen
  io.csr_wdata := exu.io.csr_wdata
  io.is_ecall := exu.io.is_ecall
  io.is_mret := exu.io.is_mret
}

class ExecutionSpec extends AnyFunSuite with ChiselSim {
  private val mask = (BigInt(1) << 32) - 1
  private def u(x: BigInt): BigInt = x & mask
  private def signed(x: BigInt): BigInt = if (x.testBit(31)) x - (BigInt(1) << 32) else x
  private def r(f3: Int, f7: Int = 0): BigInt =
    BigInt((f7 << 25) | (2 << 20) | (1 << 15) | (f3 << 12) | (3 << 7) | 0x33)
  private def i(f3: Int, imm: Int): BigInt =
    BigInt(((imm & 0xfff) << 20) | (1 << 15) | (f3 << 12) | (3 << 7) | 0x13) & mask

  test("PC uses the implicit reset address configuration") {
    implicit val cfg: CoreConfig = CoreConfig(entryAddr = 0x30000000L)
    simulate(new azazerv.ifu.PC) { dut =>
      dut.io.jump_en.poke(false)
      dut.io.jump_addr.poke(0)
      dut.io.pc_en.poke(false)
      dut.io.pc.expect(0x30000000)
      dut.io.pc_en.poke(true)
      dut.clock.step()
      dut.io.pc.expect(0x30000004)
      dut.io.jump_addr.poke(0x30000100)
      dut.io.jump_en.poke(true)
      dut.clock.step()
      dut.io.pc.expect(0x30000100)
    }
  }

  for ((name, cfg) <- Seq(
    "fast multiplication" -> CoreConfig(),
    "iterative multiplication" -> CoreConfig(fastMul = false),
    "RV32I without MDU" -> CoreConfig(hasRVM = false)
  )) {
    test(s"decode and execute with $name") {
      implicit val config: CoreConfig = cfg
      simulate(new DecodeExecuteHarness) { dut =>
        val pc = BigInt("80000100", 16)
        dut.io.in.valid.poke(false)
        dut.io.in.bits.inst_valid.poke(true)
        dut.io.in.bits.pc.poke(pc)
        dut.io.in.bits.inst.poke(0)
        dut.io.rs1.poke(0)
        dut.io.rs2.poke(0)
        dut.io.csr_rdata.poke(0x55)
        dut.io.out.ready.poke(false)
        dut.clock.step()

        def check(inst: BigInt, a: BigInt, b: BigInt, expected: BigInt,
                  jump: Option[BigInt] = None, csr: Boolean = false,
                  ecall: Boolean = false, mret: Boolean = false,
                  validInstruction: Boolean = true, hold: Int = 3): Unit = {
          val clue = s"inst=0x${inst.toString(16)}, a=0x${u(a).toString(16)}, b=0x${u(b).toString(16)}"
          dut.io.in.bits.inst.poke(inst)
          dut.io.in.bits.inst_valid.poke(validInstruction)
          dut.io.rs1.poke(u(a))
          dut.io.rs2.poke(u(b))
          dut.io.in.valid.poke(true)
          dut.io.out.ready.poke(false)
          var cycles = 0
          while (!dut.io.out.valid.peekBoolean() && cycles < 70) {
            dut.io.in.ready.expect(false)
            dut.io.jump_en.expect(false)
            dut.io.csr_wen.expect(false)
            dut.clock.step()
            cycles += 1
          }
          assert(cycles < 70, s"execution timed out: $clue")
          for (_ <- 0 until hold) {
            dut.io.out.valid.expect(true)
            dut.io.in.ready.expect(false)
            dut.io.out.bits.alu_result.expect(u(expected), clue)
            dut.io.out.bits.pc.expect(pc)
            dut.io.out.bits.inst.expect(inst)
            dut.io.jump_en.expect(false)
            dut.io.csr_wen.expect(false)
            dut.io.is_ecall.expect(false)
            dut.io.is_mret.expect(false)
            dut.clock.step()
          }
          dut.io.out.ready.poke(true)
          dut.io.in.ready.expect(true)
          dut.io.out.valid.expect(true)
          dut.io.out.bits.alu_result.expect(u(expected), clue)
          dut.io.jump_en.expect(jump.isDefined)
          jump.foreach(target => dut.io.jump_addr.expect(u(target), clue))
          dut.io.csr_wen.expect(csr)
          dut.io.is_ecall.expect(ecall)
          dut.io.is_mret.expect(mret)
          if (csr) dut.io.csr_wdata.expect(u(a))
          dut.clock.step()
          dut.io.in.valid.poke(false)
          dut.io.out.valid.expect(false)
          dut.io.jump_en.expect(false)
          dut.io.csr_wen.expect(false)
        }

        // Every integer operation; deliberately unrelated rs1/rs2 for U and jump instructions.
        check(r(0), 7, 9, 16)
        check(r(0, 0x20), 7, 9, -2)
        check(r(1), 1, 36, 16)
        check(r(2), -1, 1, 1)
        check(r(3), -1, 1, 0)
        check(r(4), 0x55, 0x3c, 0x69)
        check(r(5), BigInt("80000000", 16), 4, 0x08000000)
        check(r(5, 0x20), BigInt("80000000", 16), 4, BigInt("f8000000", 16))
        check(r(6), 0x55, 0x3c, 0x7d)
        check(r(7), 0x55, 0x3c, 0x14)
        check(i(0, -7), 5, 0x123, -2)
        check(i(5, 0x404), BigInt("80000000", 16), 0, BigInt("f8000000", 16))
        check(BigInt("deadb1b7", 16), 0x12345678, 0x7fffffff, BigInt("deadb000", 16)) // LUI
        check(BigInt("12345197", 16), 0x12345678, 0x7fffffff, pc + BigInt("12345000", 16)) // AUIPC
        check(0x008001ef, 0x12345678, 99, pc + 4, jump = Some(pc + 8)) // JAL
        check(0x00c081e7, 0x1001, 99, pc + 4, jump = Some(0x100c)) // JALR clears bit 0
        check(0x00208463, 7, 7, 14, jump = Some(pc + 8)) // BEQ taken
        check(0x00208463, 7, 8, 15) // BEQ not taken
        check(0x00c0a183, 0x1000, 99, 0x100c) // LW address
        check(0x0020a623, 0x1000, 0x55, 0x100c) // SW address
        check(0x300091f3, 0xaa, 0, 0x55, csr = true) // CSRRW
        check(0x00000073, 0, 0, 0, jump = Some(BigInt("80001000", 16)), ecall = true)
        check(0x30200073, 0, 0, 0, jump = Some(BigInt("80002000", 16)), mret = true)
        check(0x008001ef, 0, 0, pc + 4, validInstruction = false) // flushed jump has no effect

        if (cfg.hasRVM) {
          def reference(f3: Int, a: BigInt, b: BigInt): BigInt = {
            val sa = signed(a); val sb = signed(b)
            u(f3 match {
              case 0 => a * b
              case 1 => (sa * sb) >> 32
              case 2 => (sa * b) >> 32
              case 3 => (a * b) >> 32
              case 4 => if (b == 0) mask else sa / sb
              case 5 => if (b == 0) mask else a / b
              case 6 => if (b == 0) a else sa % sb
              case 7 => if (b == 0) a else a % b
            })
          }
          val edges = Seq("0", "1", "2", "7fffffff", "80000000", "80000001", "fffffffe", "ffffffff").map(BigInt(_, 16))
          val random = new scala.util.Random(20261021L)
          val pairs = (for (a <- edges; b <- edges) yield (a, b)) ++
            (0 until 16).map(_ => (BigInt(32, random), BigInt(32, random)))
          for (f3 <- 0 until 8; (a, b) <- pairs) {
            check(r(f3, 1), a, b, reference(f3, a, b), hold = 2)
          }
          // An invalid M instruction must bypass the MDU instead of starting a transaction.
          check(r(0, 1), 7, 9, 0, validInstruction = false)
          check(i(0, 1), 9, 0, 10)
        }
      }
    }
  }
}
