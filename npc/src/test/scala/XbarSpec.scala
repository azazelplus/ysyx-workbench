package azazerv

import chisel3._
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import _root_.core.CoreConfig
import bus.AXI4MasterBus

class XbarSpec extends AnyFunSuite with ChiselSim {
  private def ports(dut: Xbar): Seq[AXI4MasterBus] = Seq(dut.io.ram, dut.io.mmio)

  private def initialize(dut: Xbar): Unit = {
    dut.io.in.ar.valid.poke(false)
    dut.io.in.ar.bits.addr.poke(0)
    dut.io.in.ar.bits.id.poke(0)
    dut.io.in.ar.bits.len.poke(0)
    dut.io.in.ar.bits.size.poke(2)
    dut.io.in.ar.bits.burst.poke(1)
    dut.io.in.r.ready.poke(false)
    dut.io.in.aw.valid.poke(false)
    dut.io.in.aw.bits.addr.poke(0)
    dut.io.in.aw.bits.id.poke(0)
    dut.io.in.aw.bits.len.poke(0)
    dut.io.in.aw.bits.size.poke(2)
    dut.io.in.aw.bits.burst.poke(1)
    dut.io.in.w.valid.poke(false)
    dut.io.in.w.bits.data.poke(0)
    dut.io.in.w.bits.strb.poke(0)
    dut.io.in.w.bits.last.poke(true)
    dut.io.in.b.ready.poke(false)
    for (port <- ports(dut)) {
      port.ar.ready.poke(false)
      port.r.valid.poke(false)
      port.r.bits.data.poke(0)
      port.r.bits.resp.poke(0)
      port.r.bits.id.poke(0)
      port.r.bits.last.poke(true)
      port.aw.ready.poke(false)
      port.w.ready.poke(false)
      port.b.valid.poke(false)
      port.b.bits.resp.poke(0)
      port.b.bits.id.poke(0)
    }
    dut.clock.step()
  }

  private def noTraffic(dut: Xbar): Unit = {
    for (port <- ports(dut)) {
      port.ar.valid.expect(false)
      port.aw.valid.expect(false)
      port.w.valid.expect(false)
      port.r.ready.expect(false)
      port.b.ready.expect(false)
    }
  }

  private def read(dut: Xbar, addr: BigInt, selected: Option[Int]): Unit = {
    for (port <- ports(dut)) {
      port.ar.ready.poke(false)
      port.r.valid.poke(false)
    }
    dut.io.in.ar.bits.addr.poke(addr)
    dut.io.in.ar.valid.poke(true)
    dut.io.in.r.ready.poke(false)
    // Requests must be visible downstream before the first clock edge.
    for ((port, index) <- ports(dut).zipWithIndex) {
      port.ar.valid.expect(selected.contains(index))
      if (selected.contains(index)) port.ar.bits.addr.expect(addr)
    }
    if (selected.isDefined) {
      for (_ <- 0 until 2) {
        dut.io.in.ar.ready.expect(false)
        dut.clock.step()
      }
    }
    ports(dut).foreach(_.ar.ready.poke(true))
    dut.io.in.ar.ready.expect(true)
    dut.clock.step()
    dut.io.in.ar.valid.poke(false)
    dut.io.in.ar.bits.addr.poke(0) // The response must still use the locked target.
    if (selected.isDefined) {
      for (_ <- 0 until 2) {
        dut.io.in.r.valid.expect(false)
        dut.clock.step()
      }
    }
    // Poison unselected response inputs to check that they cannot leak upstream.
    for (port <- ports(dut)) {
      port.r.valid.poke(true)
      port.r.bits.data.poke(0xbadf00dL)
      port.r.bits.resp.poke(1)
    }
    val data = BigInt("89abcdef", 16)
    selected.foreach { index =>
      ports(dut)(index).r.bits.data.poke(data)
      ports(dut)(index).r.bits.resp.poke(2)
    }
    for (_ <- 0 until 3) {
      dut.io.in.r.valid.expect(true)
      dut.io.in.r.bits.data.expect(if (selected.isDefined) data else BigInt(0))
      dut.io.in.r.bits.resp.expect(if (selected.isDefined) 2 else 3)
      if (selected.isEmpty) dut.io.in.ar.ready.expect(false)
      noTraffic(dut)
      dut.clock.step()
    }
    dut.io.in.r.ready.poke(true)
    for ((port, index) <- ports(dut).zipWithIndex) {
      port.r.ready.expect(selected.contains(index))
    }
    dut.clock.step()
    ports(dut).foreach(_.r.valid.poke(false))
    dut.io.in.r.ready.poke(false)
    dut.io.in.r.valid.expect(false)
    noTraffic(dut)
  }

  private def write(dut: Xbar, addr: BigInt, selected: Int, order: String): Unit = {
    for (port <- ports(dut)) {
      port.aw.ready.poke(false)
      port.w.ready.poke(false)
      port.b.valid.poke(false)
    }
    val data = BigInt("12345678", 16)
    dut.io.in.aw.bits.addr.poke(addr)
    dut.io.in.w.bits.data.poke(data)
    dut.io.in.w.bits.strb.poke(5)
    dut.io.in.w.valid.poke(true)
    if (order == "W first") {
      // W can arrive before AW; it must wait until an address supplies the target.
      for (_ <- 0 until 2) {
        dut.io.in.w.ready.expect(false)
        noTraffic(dut)
        dut.clock.step()
      }
    }
    dut.io.in.aw.valid.poke(true)

    def checkRequests(aw: Boolean, w: Boolean): Unit = {
      for ((port, index) <- ports(dut).zipWithIndex) {
        port.aw.valid.expect(index == selected && aw)
        port.w.valid.expect(index == selected && w)
      }
      if (aw) ports(dut)(selected).aw.bits.addr.expect(addr)
      if (w) {
        ports(dut)(selected).w.bits.data.expect(data)
        ports(dut)(selected).w.bits.strb.expect(5)
      }
      dut.io.in.ar.ready.expect(false)
    }

    for (_ <- 0 until 2) {
      checkRequests(aw = true, w = true)
      dut.io.in.aw.ready.expect(false)
      dut.io.in.w.ready.expect(false)
      dut.clock.step()
    }
    ports(dut)(selected).aw.ready.poke(order != "W first")
    ports(dut)(selected).w.ready.poke(order != "AW first")
    dut.clock.step()
    if (order != "W first") {
      dut.io.in.aw.valid.poke(false)
      dut.io.in.aw.bits.addr.poke(0)
      dut.io.in.aw.bits.id.poke(0)
      dut.io.in.aw.bits.len.poke(0)
      dut.io.in.aw.bits.size.poke(2)
      dut.io.in.aw.bits.burst.poke(1)
    }
    if (order != "AW first") {
      dut.io.in.w.valid.poke(false)
      dut.io.in.w.bits.data.poke(0)
    }
    if (order != "together") {
      for (_ <- 0 until 3) {
        checkRequests(aw = order == "W first", w = order == "AW first")
        dut.io.in.b.valid.expect(false)
        dut.clock.step()
      }
      ports(dut)(selected).aw.ready.poke(true)
      ports(dut)(selected).w.ready.poke(true)
      dut.clock.step()
      dut.io.in.aw.valid.poke(false)
      dut.io.in.aw.bits.addr.poke(0)
      dut.io.in.aw.bits.id.poke(0)
      dut.io.in.aw.bits.len.poke(0)
      dut.io.in.aw.bits.size.poke(2)
      dut.io.in.aw.bits.burst.poke(1)
      dut.io.in.w.valid.poke(false)
      dut.io.in.w.bits.data.poke(0)
    }
    for (_ <- 0 until 2) {
      dut.io.in.b.valid.expect(false)
      noTraffic(dut)
      dut.clock.step()
    }
    for (port <- ports(dut)) {
      port.b.valid.poke(true)
      port.b.bits.resp.poke(1)
    }
    ports(dut)(selected).b.bits.resp.poke(2)
    for (_ <- 0 until 3) {
      dut.io.in.b.valid.expect(true)
      dut.io.in.b.bits.resp.expect(2)
      noTraffic(dut)
      dut.clock.step()
    }
    dut.io.in.b.ready.poke(true)
    for ((port, index) <- ports(dut).zipWithIndex) {
      port.b.ready.expect(index == selected)
    }
    dut.clock.step()
    ports(dut).foreach(_.b.valid.poke(false))
    dut.io.in.b.ready.poke(false)
    dut.io.in.b.valid.expect(false)
    noTraffic(dut)
  }

  test("read routing checks region boundaries and isolates stalled responses") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new Xbar()) { dut =>
      initialize(dut)
      for ((addr, selected) <- Seq(
        0x80000000L -> Some(0), 0x87fffffcL -> Some(0),
        0x10000000L -> Some(1), 0xa0000048L -> Some(1), 0xa000004cL -> Some(1),
        0L -> None, 0x7ffffffcL -> None, 0x88000000L -> None,
        0x10000004L -> None, 0xa0000050L -> None, 0xfffffffcL -> None
      )) read(dut, BigInt(addr), selected)
    }
  }

  test("writes preserve addresses and strobes with independent AW and W handshakes") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new Xbar()) { dut =>
      initialize(dut)
      for ((addr, selected) <- Seq(0x80000024L -> 0, 0x10000000L -> 1);
           order <- Seq("AW first", "W first", "together")) {
        write(dut, BigInt(addr), selected, order)
      }
    }
  }

  test("unmapped writes consume AW and W before returning a stable DECERR") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new Xbar()) { dut =>
      initialize(dut)
      for (dataFirst <- Seq(false, true)) {
        dut.io.in.w.bits.data.poke(0x55)
        dut.io.in.w.bits.strb.poke(15)
        dut.io.in.w.valid.poke(dataFirst)
        if (dataFirst) {
          dut.clock.step(2)
          dut.io.in.w.ready.expect(false)
          noTraffic(dut)
        }
        dut.io.in.aw.bits.addr.poke(0x20000000L)
        dut.io.in.aw.valid.poke(true)
        dut.io.in.aw.ready.expect(true)
        dut.clock.step()
        dut.io.in.aw.valid.poke(false)
        dut.io.in.aw.bits.addr.poke(0x80000000L)
        if (!dataFirst) {
          for (_ <- 0 until 3) {
            dut.io.in.aw.ready.expect(false)
            dut.io.in.b.valid.expect(false)
            noTraffic(dut)
            dut.clock.step()
          }
          dut.io.in.w.valid.poke(true)
          dut.io.in.w.ready.expect(true)
          dut.clock.step()
        }
        dut.io.in.w.valid.poke(false)
        for (_ <- 0 until 3) {
          dut.io.in.b.valid.expect(true)
          dut.io.in.b.bits.resp.expect(3)
          dut.io.in.aw.ready.expect(false)
          dut.io.in.w.ready.expect(false)
          noTraffic(dut)
          dut.clock.step()
        }
        dut.io.in.b.ready.poke(true)
        dut.clock.step()
        dut.io.in.b.ready.poke(false)
        dut.io.in.b.valid.expect(false)
      }
    }
  }

  test("mapped requests and responses pass without an extra routing cycle") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new Xbar()) { dut =>
      initialize(dut)
      ports(dut).foreach(_.ar.ready.poke(true))
      dut.io.in.ar.valid.poke(true)
      dut.io.in.ar.bits.addr.poke(0x80000000L)
      dut.io.in.ar.ready.expect(true)
      dut.io.ram.ar.valid.expect(true)
      dut.io.mmio.ar.valid.expect(false)
      dut.clock.step()
      dut.io.in.ar.valid.poke(false)
      dut.io.in.ar.bits.addr.poke(0xa0000048L)
      dut.io.ram.r.valid.poke(true)
      dut.io.ram.r.bits.data.poke(0x123)
      dut.io.in.r.ready.poke(true)
      dut.io.in.r.valid.expect(true)
      dut.io.in.r.bits.data.expect(0x123)
      dut.clock.step()
      dut.io.ram.r.valid.poke(false)
      dut.io.in.r.ready.poke(false)

      dut.io.mmio.aw.ready.poke(true)
      dut.io.mmio.w.ready.poke(true)
      dut.io.in.aw.bits.addr.poke(0x10000000L)
      dut.io.in.aw.valid.poke(true)
      dut.io.in.w.bits.data.poke(0x456)
      dut.io.in.w.bits.strb.poke(1)
      dut.io.in.w.valid.poke(true)
      dut.io.in.aw.ready.expect(true)
      dut.io.in.w.ready.expect(true)
      dut.io.mmio.aw.valid.expect(true)
      dut.io.mmio.w.valid.expect(true)
      dut.io.ram.aw.valid.expect(false)
      dut.clock.step()
      dut.io.in.aw.valid.poke(false)
      dut.io.in.w.valid.poke(false)
      dut.io.mmio.b.valid.poke(true)
      dut.io.in.b.ready.poke(true)
      dut.io.in.b.valid.expect(true)
      dut.io.mmio.b.ready.expect(true)
      dut.clock.step()
      dut.io.mmio.b.valid.poke(false)
      dut.io.in.b.ready.poke(false)
      noTraffic(dut)
    }
  }

  test("address parameters are independent of the reset PC and handle the RV32 upper boundary") {
    implicit val cfg: CoreConfig = CoreConfig()
    simulate(new Xbar(ramBase = 0xfffff000L, ramSize = 0x1000L,
      uartBase = 0x20000000L, rtcBase = 0x20001000L)) { dut =>
      initialize(dut)
      read(dut, BigInt(0xfffff000L), Some(0))
      read(dut, BigInt(0xfffffffcL), Some(0))
      read(dut, BigInt(0x20000000L), Some(1))
      read(dut, BigInt(0x20001004L), Some(1))
      read(dut, BigInt(0x80000000L), None)
      read(dut, BigInt(0), None)
    }
  }
}
