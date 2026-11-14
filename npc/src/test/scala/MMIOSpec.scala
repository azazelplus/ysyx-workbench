package azazerv

import chisel3._
import chisel3.simulator.HasSimulator
import chisel3.simulator.scalatest.ChiselSim
import org.scalatest.funsuite.AnyFunSuite
import java.nio.file.{Files, Paths}
import scala.sys.process._
import _root_.core.CoreConfig
import device.MMIO

class MMIOSpec extends AnyFunSuite with ChiselSim {
  // Link a deterministic C++ device model into each simulation executable.
  private implicit lazy val simulator: HasSimulator = {
    val root = Paths.get("").toAbsolutePath
    val build = Files.createTempDirectory("npc-mmio-model-")
    val obj = build.resolve("mmio_model.o").toString
    val library = build.resolve("mmio_model.a").toString
    assert(Process(Seq("g++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-c",
      "-I" + root.resolve("sim/src"), root.resolve("src/test/resources/mmio_model.cpp").toString,
      "-o", obj)).! == 0)
    assert(Process(Seq("ar", "rcs", library, obj)).! == 0)
    HasSimulator.simulators.verilator(
      compilationSettings = svsim.CommonCompilationSettings(linkLibraryPaths = Seq(library)))
  }

  private def initialize(dut: MMIO): Unit = {
    dut.io.axi.ar.valid.poke(false)
    dut.io.axi.ar.bits.addr.poke(0)
    dut.io.axi.ar.bits.id.poke(0)
    dut.io.axi.ar.bits.len.poke(0)
    dut.io.axi.ar.bits.size.poke(2)
    dut.io.axi.ar.bits.burst.poke(1)
    dut.io.axi.r.ready.poke(false)
    dut.io.axi.aw.valid.poke(false)
    dut.io.axi.aw.bits.addr.poke(0)
    dut.io.axi.aw.bits.id.poke(0)
    dut.io.axi.aw.bits.len.poke(0)
    dut.io.axi.aw.bits.size.poke(2)
    dut.io.axi.aw.bits.burst.poke(1)
    dut.io.axi.w.valid.poke(false)
    dut.io.axi.w.bits.data.poke(0)
    dut.io.axi.w.bits.strb.poke(0)
    dut.io.axi.w.bits.last.poke(true)
    dut.io.axi.b.ready.poke(false)
    dut.clock.step()
  }

  private def readModel(dut: MMIO, addr: BigInt, expected: BigInt): Unit = {
    dut.io.axi.ar.bits.addr.poke(addr)
    dut.io.axi.ar.bits.id.poke(6)
    dut.io.axi.ar.valid.poke(true)
    dut.io.axi.ar.ready.expect(true)
    dut.clock.step()
    dut.io.axi.ar.valid.poke(false)
    dut.io.axi.r.valid.expect(true)
    dut.io.axi.r.bits.id.expect(6)
    dut.io.axi.r.bits.data.expect(expected)
    dut.io.axi.r.bits.resp.expect(0)
    dut.io.axi.r.bits.last.expect(true)
    dut.io.axi.r.ready.poke(true)
    dut.clock.step()
    dut.io.axi.r.ready.poke(false)
  }

  test("a stalled read response holds one sampled device value and its ID") {
    implicit val cfg: CoreConfig = CoreConfig(axiIdBits = 6)
    simulate(new MMIO) { dut =>
      initialize(dut)
      dut.io.axi.ar.bits.addr.poke(0xa0000048L)
      dut.io.axi.ar.bits.id.poke(45)
      dut.io.axi.ar.valid.poke(true)
      dut.clock.step()
      dut.io.axi.ar.valid.poke(false)
      dut.io.axi.ar.bits.addr.poke(0)
      dut.io.axi.ar.bits.id.poke(0)
      for (_ <- 0 until 6) {
        dut.io.axi.r.valid.expect(true)
        dut.io.axi.r.bits.data.expect(1)
        dut.io.axi.r.bits.id.expect(45)
        dut.io.axi.r.bits.resp.expect(0)
        dut.io.axi.r.bits.last.expect(true)
        dut.io.axi.ar.ready.expect(false)
        dut.clock.step()
      }
      dut.io.axi.r.ready.poke(true)
      dut.clock.step()
      dut.io.axi.r.ready.poke(false)
      dut.io.axi.r.valid.expect(false)
      readModel(dut, BigInt(0xa0000048L), BigInt(2))
    }
  }

  for (order <- Seq("AW first", "W first", "together")) {
    test(s"a write reaches the device once with $order and a stalled B response") {
      implicit val cfg: CoreConfig = CoreConfig(axiIdBits = 6)
      simulate(new MMIO) { dut =>
        initialize(dut)
        dut.io.axi.aw.bits.addr.poke(0x10000000L)
        dut.io.axi.aw.bits.id.poke(45)
        dut.io.axi.w.bits.data.poke(0x44332211L)
        dut.io.axi.w.bits.strb.poke(5)
        dut.io.axi.aw.valid.poke(order != "W first")
        dut.io.axi.w.valid.poke(order != "AW first")
        dut.io.axi.aw.ready.expect(true)
        dut.io.axi.w.ready.expect(true)
        dut.clock.step()
        if (order != "W first") {
          dut.io.axi.aw.valid.poke(false)
          dut.io.axi.aw.bits.addr.poke(0)
          dut.io.axi.aw.bits.id.poke(0)
        }
        if (order != "AW first") {
          dut.io.axi.w.valid.poke(false)
          dut.io.axi.w.bits.data.poke(0)
          dut.io.axi.w.bits.strb.poke(0)
        }
        if (order != "together") {
          for (_ <- 0 until 3) {
            dut.io.axi.b.valid.expect(false)
            dut.clock.step()
          }
          dut.io.axi.aw.valid.poke(order == "W first")
          dut.io.axi.w.valid.poke(order == "AW first")
          dut.clock.step()
          dut.io.axi.aw.valid.poke(false)
          dut.io.axi.w.valid.poke(false)
          dut.io.axi.aw.bits.addr.poke(0)
          dut.io.axi.aw.bits.id.poke(0)
          dut.io.axi.w.bits.data.poke(0)
          dut.io.axi.w.bits.strb.poke(0)
        }
        for (_ <- 0 until 6) {
          dut.io.axi.b.valid.expect(true)
          dut.io.axi.b.bits.id.expect(45)
          dut.io.axi.b.bits.resp.expect(0)
          dut.io.axi.aw.ready.expect(false)
          dut.io.axi.w.ready.expect(false)
          dut.clock.step()
        }
        dut.io.axi.b.ready.poke(true)
        dut.clock.step()
        dut.io.axi.b.ready.poke(false)
        dut.io.axi.b.valid.expect(false)
        readModel(dut, BigInt(0xa000004cL), BigInt(1))
        readModel(dut, BigInt(0xa0000050L), BigInt(0x10000000L))
        readModel(dut, BigInt(0xa0000054L), BigInt(0x44332211L))
        readModel(dut, BigInt(0xa0000058L), BigInt(5))
      }
    }
  }
}
