package azazerv

import chisel3._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import bus.AXI4MasterBus

class ArbiterXbarHarness(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val master0 = Flipped(new AXI4MasterBus(cfg.bus))
    val master1 = Flipped(new AXI4MasterBus(cfg.bus))
    val ram = new AXI4MasterBus(cfg.bus)
    val mmio = new AXI4MasterBus(cfg.bus)
  })

  val arbiter = Module(new AXI4Arbiter)
  val xbar = Module(new Xbar)
  arbiter.io.master0 <> io.master0
  arbiter.io.master1 <> io.master1
  xbar.io.in <> arbiter.io.slave
  io.ram <> xbar.io.ram
  io.mmio <> xbar.io.mmio
}

class ArbiterXbarSpec extends AnyFunSuite with ChiselSim {
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
      port.r.bits.data.poke(0)
      port.r.bits.resp.poke(0)
      port.r.bits.id.poke(0)
      port.r.bits.last.poke(true)
      port.aw.ready.poke(true)
      port.w.ready.poke(true)
      port.b.valid.poke(false)
      port.b.bits.resp.poke(0)
      port.b.bits.id.poke(0)
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

  test("one outstanding read is enforced across masters and across targets") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new ArbiterXbarHarness) { dut =>
      initialize(dut)
      // Both masters request at once. LSU wins and IFU keeps its request pending.
      dut.io.master0.ar.valid.poke(true)
      dut.io.master0.ar.bits.addr.poke(0x80000000L)
      dut.io.master1.ar.valid.poke(true)
      dut.io.master1.ar.bits.addr.poke(0xa0000048L)
      dut.clock.step() // Arbitration; Xbar must not add another setup cycle.
      dut.io.mmio.ar.valid.expect(true)
      dut.io.mmio.ar.bits.addr.expect(0xa0000048L)
      dut.io.ram.ar.valid.expect(false)
      dut.io.master0.ar.ready.expect(false)
      dut.io.master1.ar.ready.expect(true)
      dut.clock.step()

      // LSU immediately presents its next read. The downstream remains ready,
      // so only the arbiter's transaction limit can prevent a second handshake.
      dut.io.master1.ar.bits.addr.poke(0x80000024L)
      for (_ <- 0 until 3) {
        requestsBlocked(dut)
        dut.clock.step()
      }
      dut.io.mmio.r.valid.poke(true)
      dut.io.mmio.r.bits.data.poke(0x123)
      dut.io.mmio.r.bits.resp.poke(2)
      for (_ <- 0 until 3) {
        requestsBlocked(dut)
        dut.io.master0.r.valid.expect(false)
        dut.io.master1.r.valid.expect(true)
        dut.io.master1.r.bits.data.expect(0x123)
        dut.io.master1.r.bits.resp.expect(2)
        dut.io.mmio.r.ready.expect(false)
        dut.clock.step()
      }
      dut.io.master1.r.ready.poke(true)
      dut.clock.step()
      dut.io.mmio.r.valid.poke(false)

      // After the response, LSU's next read is routed to RAM.
      dut.clock.step()
      dut.io.ram.ar.valid.expect(true)
      dut.io.ram.ar.bits.addr.expect(0x80000024L)
      dut.io.mmio.ar.valid.expect(false)
      dut.clock.step()
      dut.io.master1.ar.valid.poke(false)
      dut.io.ram.r.valid.poke(true)
      dut.io.ram.r.bits.data.poke(0x456)
      dut.io.master1.r.valid.expect(true)
      dut.io.master1.r.bits.data.expect(0x456)
      dut.io.master0.r.valid.expect(false)
      dut.clock.step()
      dut.io.ram.r.valid.poke(false)

      // IFU's original request can now acquire ownership and receive its response.
      dut.clock.step()
      dut.io.ram.ar.valid.expect(true)
      dut.io.ram.ar.bits.addr.expect(0x80000000L)
      dut.io.master0.ar.ready.expect(true)
      dut.clock.step()
      dut.io.master0.ar.valid.poke(false)
      dut.io.ram.r.valid.poke(true)
      dut.io.ram.r.bits.data.poke(0x789)
      dut.io.master0.r.ready.poke(true)
      dut.io.master0.r.valid.expect(true)
      dut.io.master0.r.bits.data.expect(0x789)
      dut.io.master1.r.valid.expect(false)
      dut.clock.step()
    }
  }

  for (order <- Seq("AW first", "W first", "together")) {
    test(s"a queued write waits for the previous B response with $order") {
      implicit val cfg: CoreConfig = CoreConfig()
      simulate(new ArbiterXbarHarness) { dut =>
        initialize(dut)
        dut.io.master1.w.valid.poke(true)
        dut.io.master1.w.bits.data.poke(0x12345678)
        dut.io.master1.w.bits.strb.poke(5)
        if (order == "W first") {
          // Before AW exists there is no address to decode and W must wait.
          for (_ <- 0 until 2) {
            dut.io.master1.w.ready.expect(false)
            dut.io.ram.w.valid.expect(false)
            dut.io.mmio.w.valid.expect(false)
            dut.clock.step()
          }
        }
        dut.io.master1.aw.valid.poke(true)
        dut.io.master1.aw.bits.addr.poke(0x80000024L)
        dut.io.ram.aw.ready.poke(order != "W first")
        dut.io.ram.w.ready.poke(order != "AW first")
        dut.clock.step()
        dut.io.master0.ar.valid.poke(true) // IFU also waits behind this write.
        dut.io.master0.ar.bits.addr.poke(0x80000000L)
        dut.io.ram.aw.valid.expect(true)
        dut.io.ram.w.valid.expect(true)
        dut.io.ram.aw.bits.addr.expect(0x80000024L)
        dut.io.ram.w.bits.data.expect(0x12345678)
        dut.io.ram.w.bits.strb.expect(5)
        dut.clock.step()

        // Each accepted channel advances to the next write immediately. The
        // unaccepted channel must keep carrying the original write's payload.
        if (order != "W first") dut.io.master1.aw.bits.addr.poke(0x10000000L)
        if (order != "AW first") {
          dut.io.master1.w.bits.data.poke(0x55)
          dut.io.master1.w.bits.strb.poke(2)
        }
        if (order != "together") {
          for (_ <- 0 until 3) {
            dut.io.ram.aw.valid.expect(order == "W first")
            dut.io.ram.w.valid.expect(order == "AW first")
            dut.io.mmio.aw.valid.expect(false)
            dut.io.mmio.w.valid.expect(false)
            dut.io.master1.aw.ready.expect(false)
            dut.io.master1.w.ready.expect(false)
            dut.io.master1.b.valid.expect(false)
            dut.io.master0.ar.ready.expect(false)
            if (order == "AW first") {
              dut.io.ram.w.bits.data.expect(0x12345678)
              dut.io.ram.w.bits.strb.expect(5)
            } else {
              dut.io.ram.aw.bits.addr.expect(0x80000024L)
            }
            dut.clock.step()
          }
          dut.io.ram.aw.ready.poke(true)
          dut.io.ram.w.ready.poke(true)
          dut.clock.step()
          dut.io.master1.aw.bits.addr.poke(0x10000000L)
          dut.io.master1.w.bits.data.poke(0x55)
          dut.io.master1.w.bits.strb.poke(2)
        }
        for (_ <- 0 until 2) {
          requestsBlocked(dut)
          dut.io.master1.b.valid.expect(false)
          dut.clock.step()
        }
        dut.io.ram.b.valid.poke(true)
        dut.io.ram.b.bits.resp.poke(2)
        for (_ <- 0 until 3) {
          requestsBlocked(dut)
          dut.io.master1.b.valid.expect(true)
          dut.io.master1.b.bits.resp.expect(2)
          dut.io.master0.b.valid.expect(false)
          dut.io.master0.r.valid.expect(false)
          dut.io.ram.b.ready.expect(false)
          dut.clock.step()
        }
        dut.io.master1.b.ready.poke(true)
        dut.clock.step()
        dut.io.ram.b.valid.poke(false)

        // The queued write now goes to MMIO with its own address, data and mask.
        dut.clock.step()
        dut.io.ram.aw.valid.expect(false)
        dut.io.ram.w.valid.expect(false)
        dut.io.mmio.aw.valid.expect(true)
        dut.io.mmio.aw.bits.addr.expect(0x10000000L)
        dut.io.mmio.w.valid.expect(true)
        dut.io.mmio.w.bits.data.expect(0x55)
        dut.io.mmio.w.bits.strb.expect(2)
        dut.io.master1.aw.ready.expect(true)
        dut.io.master1.w.ready.expect(true)
        dut.clock.step()
        dut.io.master1.aw.valid.poke(false)
        dut.io.master1.w.valid.poke(false)
        dut.io.mmio.b.valid.poke(true)
        dut.io.master1.b.valid.expect(true)
        dut.io.master1.b.bits.resp.expect(0)
        dut.clock.step()
        dut.io.mmio.b.valid.poke(false)

        dut.clock.step()
        dut.io.ram.ar.valid.expect(true)
        dut.io.ram.ar.bits.addr.expect(0x80000000L)
        dut.clock.step()
        dut.io.master0.ar.valid.poke(false)
        dut.io.ram.r.valid.poke(true)
        dut.io.master0.r.ready.poke(true)
        dut.io.master0.r.valid.expect(true)
        dut.io.master1.r.valid.expect(false)
        dut.clock.step()
      }
    }
  }

  test("local decode errors complete through the arbiter after the required handshakes") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new ArbiterXbarHarness) { dut =>
      initialize(dut)
      dut.io.master1.ar.valid.poke(true)
      dut.io.master1.ar.bits.addr.poke(0x20000000L)
      dut.clock.step()
      dut.io.master1.ar.ready.expect(true)
      dut.clock.step()
      dut.io.master1.ar.valid.poke(false)
      for (_ <- 0 until 3) {
        requestsBlocked(dut)
        dut.io.master1.r.valid.expect(true)
        dut.io.master1.r.bits.data.expect(0)
        dut.io.master1.r.bits.resp.expect(3)
        dut.clock.step()
      }
      dut.io.master1.r.ready.poke(true)
      dut.clock.step()

      dut.io.master1.aw.valid.poke(true)
      dut.io.master1.aw.bits.addr.poke(0x20000000L)
      dut.clock.step()
      dut.io.master1.aw.ready.expect(true)
      dut.clock.step()
      dut.io.master1.aw.valid.poke(false)
      for (_ <- 0 until 3) {
        dut.io.master1.b.valid.expect(false)
        dut.clock.step()
      }
      dut.io.master1.w.valid.poke(true)
      dut.io.master1.w.bits.data.poke(0x55)
      dut.io.master1.w.bits.strb.poke(1)
      dut.io.master1.w.ready.expect(true)
      dut.clock.step()
      dut.io.master1.w.valid.poke(false)
      for (_ <- 0 until 3) {
        requestsBlocked(dut)
        dut.io.master1.b.valid.expect(true)
        dut.io.master1.b.bits.resp.expect(3)
        dut.clock.step()
      }
      dut.io.master1.b.ready.poke(true)
      dut.clock.step()
    }
  }
}
