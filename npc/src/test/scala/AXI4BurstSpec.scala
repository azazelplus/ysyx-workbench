package azazerv

import chisel3._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import bus.{AXI4ErrorSlave, AXI4MasterBus, BusConfig}

/** Exercise complete AXI4 transactions through the arbiter and address router. */
class AXI4BurstSpec extends AnyFunSuite with ChiselSim {
  private def masters(dut: ArbiterXbarHarness): Seq[AXI4MasterBus] =
    Seq(dut.io.master0, dut.io.master1)

  private def ports(dut: ArbiterXbarHarness): Seq[AXI4MasterBus] =
    Seq(dut.io.ram, dut.io.mmio)

  private def initialize(dut: ArbiterXbarHarness): Unit = {
    for (master <- masters(dut)) {
      master.ar.valid.poke(false)
      master.ar.bits.addr.poke(0)
      master.ar.bits.id.poke(0)
      master.ar.bits.len.poke(0)
      master.ar.bits.size.poke(2)
      master.ar.bits.burst.poke(1)
      master.r.ready.poke(false)
      master.aw.valid.poke(false)
      master.aw.bits.addr.poke(0)
      master.aw.bits.id.poke(0)
      master.aw.bits.len.poke(0)
      master.aw.bits.size.poke(2)
      master.aw.bits.burst.poke(1)
      master.w.valid.poke(false)
      master.w.bits.data.poke(0)
      master.w.bits.strb.poke(0)
      master.w.bits.last.poke(true)
      master.b.ready.poke(false)
    }
    for (port <- ports(dut)) {
      port.ar.ready.poke(true)
      port.r.valid.poke(false)
      port.r.bits.id.poke(0)
      port.r.bits.data.poke(0)
      port.r.bits.resp.poke(0)
      port.r.bits.last.poke(true)
      port.aw.ready.poke(true)
      port.w.ready.poke(true)
      port.b.valid.poke(false)
      port.b.bits.id.poke(0)
      port.b.bits.resp.poke(0)
    }
    dut.clock.step()
  }

  private def requestsBlocked(dut: ArbiterXbarHarness): Unit = {
    for (master <- masters(dut)) {
      master.ar.ready.expect(false)
      master.aw.ready.expect(false)
      master.w.ready.expect(false)
    }
    for (port <- ports(dut)) {
      port.ar.valid.expect(false)
      port.aw.valid.expect(false)
      port.w.valid.expect(false)
    }
  }

  for (owner <- Seq(0, 1)) {
    test(s"master $owner retains the RAM route and ownership through a stalled read burst") {
      implicit val cfg: CoreConfig = CoreConfig(axiIdBits = 6)
      simulate(new ArbiterXbarHarness) { dut =>
        initialize(dut)
        val source = masters(dut)(owner)
        val other = masters(dut)(1 - owner)
        source.ar.valid.poke(true)
        source.ar.bits.addr.poke(0x80000020L)
        source.ar.bits.id.poke(45)
        source.ar.bits.len.poke(3)
        dut.io.ram.ar.ready.poke(false)
        dut.clock.step()
        for (_ <- 0 until 2) {
          dut.io.ram.ar.valid.expect(true)
          dut.io.ram.ar.bits.addr.expect(0x80000020L)
          dut.io.ram.ar.bits.id.expect(45)
          dut.io.ram.ar.bits.len.expect(3)
          dut.io.ram.ar.bits.size.expect(2)
          dut.io.ram.ar.bits.burst.expect(1)
          source.ar.ready.expect(false)
          dut.io.mmio.ar.valid.expect(false)
          dut.clock.step()
        }
        dut.io.ram.ar.ready.poke(true)
        dut.clock.step()

        // Both masters now have another request for MMIO. Neither may be accepted
        // until the last RAM beat has been delivered to the original owner.
        for (master <- masters(dut)) {
          master.ar.valid.poke(true)
          master.ar.bits.addr.poke(0xa0000048L)
          master.ar.bits.id.poke(12)
          master.ar.bits.len.poke(0)
        }
        for (beat <- 0 until 4) {
          source.r.ready.poke(false)
          dut.io.ram.r.valid.poke(true)
          dut.io.ram.r.bits.id.poke(45)
          dut.io.ram.r.bits.data.poke(0x100 + beat)
          dut.io.ram.r.bits.last.poke(beat == 3)
          for (_ <- 0 until 2) {
            requestsBlocked(dut)
            source.r.valid.expect(true)
            source.r.bits.id.expect(45)
            source.r.bits.data.expect(0x100 + beat)
            source.r.bits.last.expect(beat == 3)
            other.r.valid.expect(false)
            dut.io.ram.r.ready.expect(false)
            dut.io.mmio.r.ready.expect(false)
            dut.clock.step()
          }
          source.r.ready.poke(true)
          dut.io.ram.r.ready.expect(true)
          dut.clock.step()
        }
        dut.io.ram.r.valid.poke(false)
        dut.clock.step() // Fixed priority grants LSU's waiting MMIO request.
        dut.io.mmio.ar.valid.expect(true)
        dut.io.mmio.ar.bits.id.expect(12)
        dut.io.mmio.ar.bits.len.expect(0)
        dut.io.mmio.ar.bits.addr.expect(0xa0000048L)
        dut.io.ram.ar.valid.expect(false)
        dut.clock.step()
        dut.io.master1.ar.valid.poke(false)
        dut.io.mmio.r.valid.poke(true)
        dut.io.mmio.r.bits.id.poke(12)
        dut.io.master1.r.ready.poke(true)
        dut.io.master1.r.valid.expect(true)
        dut.io.master1.r.bits.id.expect(12)
        dut.io.master0.r.valid.expect(false)
        dut.clock.step()
        dut.io.mmio.r.valid.poke(false)
        dut.clock.step()
        dut.io.master0.ar.ready.expect(true)
        dut.clock.step()
        dut.io.master0.ar.valid.poke(false)
        dut.io.mmio.r.valid.poke(true)
        dut.io.master0.r.ready.poke(true)
        dut.io.master0.r.valid.expect(true)
        dut.io.master1.r.valid.expect(false)
        dut.clock.step()
      }
    }
  }

  test("write data continues until WLAST and the next write waits for B") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new ArbiterXbarHarness) { dut =>
      initialize(dut)
      val source = dut.io.master1
      source.aw.valid.poke(true)
      source.aw.bits.addr.poke(0x80000040L)
      source.aw.bits.id.poke(7)
      source.aw.bits.len.poke(3)
      source.w.valid.poke(true)
      source.w.bits.last.poke(false)
      dut.io.ram.aw.ready.poke(false)
      dut.clock.step()
      for (beat <- 0 until 4) {
        source.w.bits.data.poke(0x200 + beat)
        source.w.bits.strb.poke(Seq(1, 3, 12, 15)(beat))
        source.w.bits.last.poke(beat == 3)
        if (beat == 1) dut.io.ram.aw.ready.poke(true)
        if (beat == 2) {
          dut.io.ram.w.ready.poke(false)
          for (_ <- 0 until 2) {
            source.w.ready.expect(false)
            dut.io.ram.w.valid.expect(true)
            dut.io.ram.w.bits.data.expect(0x200 + beat)
            dut.io.ram.w.bits.last.expect(false)
            dut.io.mmio.w.valid.expect(false)
            dut.clock.step()
          }
          dut.io.ram.w.ready.poke(true)
        }
        dut.io.ram.aw.valid.expect(beat < 2)
        if (beat < 2) {
          dut.io.ram.aw.bits.id.expect(7)
          dut.io.ram.aw.bits.len.expect(3)
          dut.io.ram.aw.bits.size.expect(2)
          dut.io.ram.aw.bits.burst.expect(1)
        }
        dut.io.ram.w.valid.expect(true)
        dut.io.ram.w.bits.data.expect(0x200 + beat)
        dut.io.ram.w.bits.strb.expect(Seq(1, 3, 12, 15)(beat))
        dut.io.ram.w.bits.last.expect(beat == 3)
        dut.io.mmio.w.valid.expect(false)
        source.w.ready.expect(true)
        source.b.valid.expect(false)
        dut.clock.step()
        if (beat == 1) {
          // AW for the next transaction may appear while W for the first continues.
          source.aw.bits.addr.poke(0x10000000L)
          source.aw.bits.id.poke(8)
          source.aw.bits.len.poke(0)
        }
      }
      source.w.bits.data.poke(0x55)
      source.w.bits.strb.poke(1)
      for (_ <- 0 until 2) {
        requestsBlocked(dut)
        dut.clock.step()
      }
      dut.io.ram.b.valid.poke(true)
      dut.io.ram.b.bits.id.poke(7)
      dut.io.ram.b.bits.resp.poke(2)
      for (_ <- 0 until 2) {
        requestsBlocked(dut)
        source.b.valid.expect(true)
        source.b.bits.id.expect(7)
        source.b.bits.resp.expect(2)
        dut.io.ram.b.ready.expect(false)
        dut.clock.step()
      }
      source.b.ready.poke(true)
      dut.clock.step()
      dut.io.ram.b.valid.poke(false)
      dut.clock.step()
      dut.io.mmio.aw.valid.expect(true)
      dut.io.mmio.aw.bits.id.expect(8)
      dut.io.mmio.aw.bits.len.expect(0)
      dut.io.mmio.aw.bits.addr.expect(0x10000000L)
      dut.io.mmio.w.valid.expect(true)
      dut.io.mmio.w.bits.data.expect(0x55)
      dut.io.mmio.w.bits.last.expect(true)
      dut.clock.step()
      source.aw.valid.poke(false)
      source.w.valid.poke(false)
      dut.io.mmio.b.valid.poke(true)
      dut.io.mmio.b.bits.id.poke(8)
      source.b.valid.expect(true)
      source.b.bits.id.expect(8)
      dut.clock.step()
    }
  }

  for (beats <- Seq(1, 4, 256)) {
    test(s"an unmapped $beats-beat read returns its ID and exactly one final RLAST") {
      implicit val cfg: CoreConfig = CoreConfig(axiIdBits = 6)
      simulate(new ArbiterXbarHarness) { dut =>
        initialize(dut)
        val source = dut.io.master1
        source.ar.valid.poke(true)
        source.ar.bits.addr.poke(0x20000000L)
        source.ar.bits.id.poke(45)
        source.ar.bits.len.poke(beats - 1)
        dut.clock.step()
        source.ar.ready.expect(true)
        dut.clock.step()
        source.ar.valid.poke(false)
        source.ar.bits.id.poke(0)
        source.ar.bits.len.poke(0)
        source.ar.bits.addr.poke(0x80000000L)
        for (beat <- 0 until beats) {
          def check(): Unit = {
            requestsBlocked(dut)
            source.r.valid.expect(true)
            source.r.bits.id.expect(45)
            source.r.bits.data.expect(0)
            source.r.bits.resp.expect(3)
            source.r.bits.last.expect(beat == beats - 1)
          }
          source.r.ready.poke(false)
          if (beat == 0 || beat == beats / 2 || beat == beats - 1) {
            for (_ <- 0 until 2) {
              check()
              dut.clock.step()
            }
          }
          check()
          source.r.ready.poke(true)
          dut.clock.step()
        }
        source.r.valid.expect(false)
        dut.clock.step()
        source.r.valid.expect(false)
      }
    }
  }

  for (order <- Seq("AW first", "W first", "together")) {
    test(s"the error slave drains a write burst and returns AWID with $order") {
      val cfg = BusConfig(addrBits = 32, dataBits = 32, idBits = 6)
      simulate(new AXI4ErrorSlave(cfg)) { dut =>
        dut.io.axi.ar.valid.poke(false)
        dut.io.axi.ar.bits.addr.poke(0)
        dut.io.axi.ar.bits.id.poke(0)
        dut.io.axi.ar.bits.len.poke(0)
        dut.io.axi.ar.bits.size.poke(2)
        dut.io.axi.ar.bits.burst.poke(1)
        dut.io.axi.r.ready.poke(false)
        dut.io.axi.aw.valid.poke(false)
        dut.io.axi.aw.bits.addr.poke(0x20000000L)
        dut.io.axi.aw.bits.id.poke(45)
        dut.io.axi.aw.bits.len.poke(3)
        dut.io.axi.aw.bits.size.poke(2)
        dut.io.axi.aw.bits.burst.poke(1)
        dut.io.axi.w.valid.poke(false)
        dut.io.axi.w.bits.data.poke(0)
        dut.io.axi.w.bits.strb.poke(15)
        dut.io.axi.w.bits.last.poke(false)
        dut.io.axi.b.ready.poke(false)
        dut.clock.step()
        if (order == "AW first") {
          dut.io.axi.aw.valid.poke(true)
          dut.clock.step()
          dut.io.axi.aw.valid.poke(false)
          dut.io.axi.aw.bits.id.poke(0)
          dut.io.axi.b.valid.expect(false)
          dut.clock.step(2)
        }
        if (order == "together") dut.io.axi.aw.valid.poke(true)
        for (beat <- 0 until 4) {
          dut.io.axi.w.valid.poke(true)
          dut.io.axi.w.bits.data.poke(beat)
          dut.io.axi.w.bits.last.poke(beat == 3)
          dut.io.axi.w.ready.expect(true)
          dut.io.axi.b.valid.expect(false)
          dut.clock.step()
          if (order == "together" && beat == 0) {
            dut.io.axi.aw.valid.poke(false)
            dut.io.axi.aw.bits.id.poke(0)
          }
        }
        dut.io.axi.w.valid.poke(false)
        if (order == "W first") {
          dut.io.axi.b.valid.expect(false)
          dut.io.axi.w.ready.expect(false)
          dut.clock.step(2)
          dut.io.axi.aw.valid.poke(true)
          dut.io.axi.aw.ready.expect(true)
          dut.clock.step()
          dut.io.axi.aw.valid.poke(false)
          dut.io.axi.aw.bits.id.poke(0)
        }
        for (_ <- 0 until 3) {
          dut.io.axi.b.valid.expect(true)
          dut.io.axi.b.bits.id.expect(45)
          dut.io.axi.b.bits.resp.expect(3)
          dut.io.axi.aw.ready.expect(false)
          dut.io.axi.w.ready.expect(false)
          dut.clock.step()
        }
        dut.io.axi.b.ready.poke(true)
        dut.clock.step()
        dut.io.axi.b.valid.expect(false)
        dut.io.axi.aw.ready.expect(true)
        dut.io.axi.w.ready.expect(true)
      }
    }
  }
}
