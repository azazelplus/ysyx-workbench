package azazerv

import chisel3._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import azazerv.idu.MemSize
import azazerv.lsu.LSU

class LSUHandshakeSpec extends AnyFunSuite with ChiselSim {
  private val base = BigInt("80000100", 16)
  private val pc = BigInt("80000040", 16)

  private def initialize(dut: LSU): Unit = {
    dut.io.in.valid.poke(false)
    dut.io.in.bits.inst_valid.poke(true)
    dut.io.in.bits.pc.poke(pc)
    dut.io.in.bits.inst.poke(0)
    dut.io.in.bits.alu_result.poke(base)
    dut.io.in.bits.store_data.poke(0)
    dut.io.in.bits.rd_addr.poke(3)
    dut.io.in.bits.mem_ren.poke(false)
    dut.io.in.bits.mem_wen.poke(false)
    dut.io.in.bits.mem_size.poke(MemSize.word)
    dut.io.in.bits.load_unsigned.poke(false)
    dut.io.in.bits.reg_wen.poke(false)
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
  }

  private def accept(dut: LSU): Unit = {
    dut.io.in.valid.poke(true)
    dut.io.in.ready.expect(true)
    dut.clock.step()
    dut.io.in.valid.poke(false)
    // Upstream changes after acceptance must not change this transaction.
    dut.io.in.bits.alu_result.poke(0)
    dut.io.in.bits.store_data.poke(0)
    dut.io.in.bits.inst.poke(0)
  }

  private def finish(dut: LSU, inst: BigInt, data: BigInt, writeback: Boolean): Unit = {
    for (_ <- 0 until 3) {
      dut.io.out.valid.expect(true)
      dut.io.in.ready.expect(false)
      dut.io.out.bits.inst_valid.expect(true)
      dut.io.out.bits.pc.expect(pc)
      dut.io.out.bits.inst.expect(inst)
      dut.io.out.bits.reg_wen.expect(writeback)
      dut.io.out.bits.wb_data.expect(data)
      dut.io.out.bits.rd_addr.expect(3)
      dut.io.axi.ar.valid.expect(false)
      dut.io.axi.aw.valid.expect(false)
      dut.io.axi.w.valid.expect(false)
      dut.clock.step()
    }
    dut.io.out.ready.poke(true)
    dut.clock.step()
    dut.io.out.ready.poke(false)
    dut.io.in.ready.expect(true)
    dut.io.out.valid.expect(false)
  }

  test("a delayed read response does not cause repeated AR handshakes") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new LSU) { dut =>
      initialize(dut)
      // Two consecutive loads also check the return to the initial request state.
      for (index <- 0 until 2) {
        val addr = base + index * 4
        val inst = BigInt("0000a183", 16) // lw x3, 0(x1)
        val data = BigInt("89abcdef", 16) + index
        dut.io.in.bits.inst.poke(inst)
        dut.io.in.bits.alu_result.poke(addr)
        dut.io.in.bits.mem_ren.poke(true)
        dut.io.in.bits.reg_wen.poke(true)
        dut.io.axi.ar.ready.poke(false)
        accept(dut)
        for (_ <- 0 until 3) {
          dut.io.axi.ar.valid.expect(true)
          dut.io.axi.ar.bits.addr.expect(addr)
          dut.io.axi.ar.bits.id.expect(0)
          dut.io.axi.ar.bits.len.expect(0)
          dut.io.axi.ar.bits.size.expect(2)
          dut.io.axi.ar.bits.burst.expect(1)
          dut.io.out.valid.expect(false)
          dut.io.in.ready.expect(false)
          dut.clock.step()
        }
        dut.io.axi.ar.ready.poke(true)
        dut.clock.step() // AR accepted; leave ready high to expose duplicate requests.
        for (_ <- 0 until 4) {
          dut.io.axi.ar.valid.expect(false)
          dut.io.axi.r.ready.expect(true)
          dut.io.out.valid.expect(false)
          dut.clock.step()
        }
        dut.io.axi.r.bits.data.poke(data)
        dut.io.axi.r.valid.poke(true)
        dut.clock.step()
        dut.io.axi.r.valid.poke(false)
        finish(dut, inst, data, writeback = true)
      }
    }
  }

  for (order <- Seq("AW first", "W first", "together")) {
    test(s"write channels handshake once with $order and a delayed B response") {
      implicit val cfg: CoreConfig = CoreConfig()
      simulate(new LSU) { dut =>
        initialize(dut)
        for (offset <- Seq(1, 3)) {
          val addr = base + offset
          val inst = BigInt((2 << 20) | (1 << 15) | (offset << 7) | 0x23) // sb x2, offset(x1)
          val byte = BigInt(0xab + offset)
          val expectedData = byte << (8 * offset)
          val expectedMask = 1 << offset
          dut.io.in.bits.inst.poke(inst)
          dut.io.in.bits.alu_result.poke(addr)
          dut.io.in.bits.store_data.poke(byte)
          dut.io.in.bits.mem_wen.poke(true)
          dut.io.in.bits.mem_size.poke(MemSize.byte)
          dut.io.axi.aw.ready.poke(false)
          dut.io.axi.w.ready.poke(false)
          accept(dut)

          def checkPending(aw: Boolean, w: Boolean): Unit = {
            dut.io.axi.aw.valid.expect(aw)
            dut.io.axi.w.valid.expect(w)
            dut.io.axi.aw.bits.addr.expect(base)
            dut.io.axi.aw.bits.id.expect(0)
            dut.io.axi.aw.bits.len.expect(0)
            dut.io.axi.aw.bits.size.expect(2)
            dut.io.axi.aw.bits.burst.expect(1)
            dut.io.axi.w.bits.data.expect(expectedData)
            dut.io.axi.w.bits.strb.expect(expectedMask)
            dut.io.axi.w.bits.last.expect(true)
            dut.io.axi.ar.valid.expect(false)
            dut.io.out.valid.expect(false)
            dut.io.in.ready.expect(false)
          }

          for (_ <- 0 until 2) {
            checkPending(aw = true, w = true)
            dut.clock.step()
          }
          dut.io.axi.aw.ready.poke(order != "W first")
          dut.io.axi.w.ready.poke(order != "AW first")
          dut.clock.step()
          if (order != "together") {
            for (_ <- 0 until 3) {
              checkPending(aw = order == "W first", w = order == "AW first")
              dut.clock.step()
            }
            dut.io.axi.aw.ready.poke(true)
            dut.io.axi.w.ready.poke(true)
            dut.clock.step()
          }
          // Both ready signals stay high while B is delayed.
          for (_ <- 0 until 4) {
            checkPending(aw = false, w = false)
            dut.io.axi.b.ready.expect(true)
            dut.clock.step()
          }
          dut.io.axi.b.valid.poke(true)
          dut.clock.step()
          dut.io.axi.b.valid.poke(false)
          finish(dut, inst, addr, writeback = false)
        }
      }
    }
  }
}
