package azazerv

import chisel3._
import chisel3.util._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import azazerv.idu.IDU
import azazerv.exu.EXU
import azazerv.lsu.LSU
import bus.AXI4MasterBus

/** Check that decoded controls survive pipeline registers and drive memory accesses. */
class MemoryAccessHarness(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new IF2ID))
    val rs1 = Input(UInt(32.W))
    val rs2 = Input(UInt(32.W))
    val axi = new AXI4MasterBus(cfg.bus)
    val out = Decoupled(new LS2WB)
  })
  val idu = Module(new IDU)
  val exu = Module(new EXU)
  val lsu = Module(new LSU)
  idu.io.in <> io.in
  idu.io.rs1_data := io.rs1
  idu.io.rs2_data := io.rs2
  exu.io.in <> Queue(idu.io.out, 1)
  exu.io.csr_rdata := 0.U
  exu.io.mtvec := 0.U
  exu.io.mepc := 0.U
  lsu.io.in <> Queue(exu.io.out, 1)
  io.axi <> lsu.io.axi
  io.out <> lsu.io.out
}

class MemoryAccessSpec extends AnyFunSuite with ChiselSim {
  test("decoded load/store controls select bytes, extension and write masks") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new MemoryAccessHarness) { dut =>
      val mask = (BigInt(1) << 32) - 1
      val pc = BigInt("80000040", 16)
      val base = BigInt("80000120", 16)
      val aligned = base - 16
      val storeData = BigInt("89abcdef", 16)
      dut.io.in.valid.poke(false)
      dut.io.in.bits.inst_valid.poke(true)
      dut.io.in.bits.pc.poke(pc)
      dut.io.in.bits.inst.poke(0)
      dut.io.rs1.poke(base)
      dut.io.rs2.poke(storeData)
      dut.io.out.ready.poke(false)
      dut.io.axi.ar.ready.poke(false)
      dut.io.axi.r.valid.poke(false)
      dut.io.axi.r.bits.data.poke(0)
      dut.io.axi.r.bits.resp.poke(0)
      dut.io.axi.r.bits.id.poke(0)
      dut.io.axi.r.bits.last.poke(true)
      dut.io.axi.aw.ready.poke(false)
      dut.io.axi.w.ready.poke(false)
      dut.io.axi.b.valid.poke(false)
      dut.io.axi.b.bits.resp.poke(0)
      dut.io.axi.b.bits.id.poke(0)
      dut.clock.step()

      def waitFor(condition: => Boolean): Unit = {
        var cycles = 0
        while (!condition && cycles < 20) {
          dut.clock.step()
          cycles += 1
        }
        assert(condition, "pipeline timed out")
      }

      def issue(inst: BigInt): Unit = {
        dut.io.in.bits.inst.poke(inst)
        dut.io.in.valid.poke(true)
        waitFor(dut.io.in.ready.peekBoolean())
        dut.clock.step()
        dut.io.in.valid.poke(false)
        // Change the upstream instruction and operands after acceptance.
        dut.io.in.bits.inst.poke(0x13)
        dut.io.rs1.poke(0)
        dut.io.rs2.poke(0)
      }

      def finish(inst: BigInt, expected: BigInt, writeback: Boolean): Unit = {
        waitFor(dut.io.out.valid.peekBoolean())
        for (_ <- 0 until 3) {
          dut.io.out.valid.expect(true)
          dut.io.out.bits.inst_valid.expect(true)
          dut.io.out.bits.inst.expect(inst)
          dut.io.out.bits.pc.expect(pc)
          dut.io.out.bits.reg_wen.expect(writeback)
          dut.io.out.bits.wb_data.expect(expected & mask)
          if (writeback) dut.io.out.bits.rd_addr.expect(3)
          dut.clock.step()
        }
        dut.io.out.ready.poke(true)
        dut.clock.step()
        dut.io.out.ready.poke(false)
        dut.io.rs1.poke(base)
        dut.io.rs2.poke(storeData)
      }

      for (word <- Seq(BigInt("807fff01", 16), BigInt("7fff0080", 16));
           (funct3, bytes, unsigned) <- Seq(
             (0, 1, false), (1, 2, false), (2, 4, false),
             (4, 1, true), (5, 2, true));
           offset <- 0 until 4 by bytes) {
        val imm = (offset - 16) & 0xfff
        val inst = BigInt((imm << 20) | (1 << 15) | (funct3 << 12) | (3 << 7) | 0x03) & mask
        issue(inst)
        waitFor(dut.io.axi.ar.valid.peekBoolean())
        for (_ <- 0 until 2) {
          dut.io.axi.ar.valid.expect(true)
          dut.io.axi.ar.bits.addr.expect(aligned)
          dut.io.axi.aw.valid.expect(false)
          dut.clock.step()
        }
        dut.io.axi.ar.ready.poke(true)
        dut.clock.step()
        dut.io.axi.ar.ready.poke(false)
        dut.io.axi.r.bits.data.poke(word)
        dut.io.axi.r.valid.poke(true)
        dut.io.axi.r.ready.expect(true)
        dut.clock.step()
        dut.io.axi.r.valid.poke(false)
        val bits = bytes * 8
        val value = (word >> (offset * 8)) & ((BigInt(1) << bits) - 1)
        val expected = if (!unsigned && value.testBit(bits - 1)) value - (BigInt(1) << bits) else value
        finish(inst, expected, writeback = true)
      }

      for ((funct3, bytes) <- Seq((0, 1), (1, 2), (2, 4));
           offset <- 0 until 4 by bytes) {
        val imm = (offset - 16) & 0xfff
        val inst = BigInt(((imm >> 5) << 25) | (2 << 20) | (1 << 15) |
          (funct3 << 12) | ((imm & 31) << 7) | 0x23) & mask
        issue(inst)
        waitFor(dut.io.axi.aw.valid.peekBoolean() && dut.io.axi.w.valid.peekBoolean())
        for (_ <- 0 until 2) {
          dut.io.axi.aw.valid.expect(true)
          dut.io.axi.w.valid.expect(true)
          dut.io.axi.ar.valid.expect(false)
          dut.io.axi.aw.bits.addr.expect(aligned)
          dut.io.axi.w.bits.strb.expect(((1 << bytes) - 1) << offset)
          dut.io.axi.w.bits.data.expect((storeData & ((BigInt(1) << (bytes * 8)) - 1)) << (offset * 8))
          dut.clock.step()
        }
        dut.io.axi.aw.ready.poke(true)
        dut.io.axi.w.ready.poke(true)
        dut.clock.step()
        dut.io.axi.aw.ready.poke(false)
        dut.io.axi.w.ready.poke(false)
        dut.io.axi.b.valid.poke(true)
        dut.io.axi.b.ready.expect(true)
        dut.clock.step()
        dut.io.axi.b.valid.poke(false)
        finish(inst, aligned + offset, writeback = false)
      }

      // A non-memory instruction passes its ALU result through without a bus request.
      val addi = BigInt("00708193", 16) // addi x3, x1, 7
      issue(addi)
      waitFor(dut.io.out.valid.peekBoolean())
      dut.io.axi.ar.valid.expect(false)
      dut.io.axi.aw.valid.expect(false)
      dut.io.axi.w.valid.expect(false)
      finish(addi, base + 7, writeback = true)
    }
  }
}
