// AXI4的接口. 提供AXI4MasterBus和AXI4SlaveBus主从接口. 
package bus

import chisel3._
import chisel3.util._

// 主机接口
class AXI4MasterBus(
    val cfg: BusConfig,
    userType: Option[Data] = None
) extends Bundle {
  val ar = Decoupled(new AXI4AR(cfg, userType))
  val r = Flipped(Decoupled(new AXI4R(cfg, userType)))
  val aw = Decoupled(new AXI4AW(cfg, userType))
  val w = Decoupled(new AXI4W(cfg, userType))
  val b = Flipped(Decoupled(new AXI4B(cfg, userType)))

  def init(): Unit = {
    ar.valid := false.B
    r.ready := false.B
    aw.valid := false.B
    w.valid := false.B
    b.ready := false.B
    ar.bits := DontCare
    aw.bits := DontCare
    w.bits := 0.U.asTypeOf(w.bits)  // 先DontCare再连线会发生塌缩. 
  }
}

// 从机接口
class AXI4SlaveBus(
    val cfg: BusConfig,
    userType: Option[Data] = None
) extends Bundle {
  val ar = Flipped(Decoupled(new AXI4AR(cfg, userType)))
  val r = Decoupled(new AXI4R(cfg, userType))
  val aw = Flipped(Decoupled(new AXI4AW(cfg, userType)))
  val w = Flipped(Decoupled(new AXI4W(cfg, userType)))
  val b = Decoupled(new AXI4B(cfg, userType))

  def init(): Unit = {
    ar.ready := false.B
    r.valid := false.B
    aw.ready := false.B
    w.ready := false.B
    b.valid := false.B
    b.bits := DontCare
    r.bits := DontCare
  }
}

// AXI四个通道
class AXI4AR(val cfg: BusConfig, userType: Option[Data] = None) extends Bundle {
  val id = UInt(cfg.idBits.W)
  val addr = UInt(cfg.addrBits.W)
  val len = UInt(8.W)
  val size = UInt(3.W)
  val burst = UInt(2.W)
  val user = userType.map(_.cloneType).getOrElse(UInt(0.W))
}

class AXI4R(val cfg: BusConfig, userType: Option[Data] = None) extends Bundle {
  val id = UInt(cfg.idBits.W)
  val data = UInt(cfg.dataBits.W)
  val resp = UInt(2.W)
  val last = Bool()
  val user = userType.map(_.cloneType).getOrElse(UInt(0.W))
}

class AXI4AW(val cfg: BusConfig, userType: Option[Data] = None) extends Bundle {
  val id = UInt(cfg.idBits.W)
  val addr = UInt(cfg.addrBits.W)
  val len = UInt(8.W)
  val size = UInt(3.W)
  val burst = UInt(2.W)
  val user = userType.map(_.cloneType).getOrElse(UInt(0.W))
}

class AXI4W(val cfg: BusConfig, userType: Option[Data] = None) extends Bundle {
  val data = UInt(cfg.dataBits.W)
  val strb = UInt(cfg.strbBits.W)
  val last = Bool()
  val user = userType.map(_.cloneType).getOrElse(UInt(0.W))
}

class AXI4B(val cfg: BusConfig, userType: Option[Data] = None) extends Bundle {
  val id = UInt(cfg.idBits.W)
  val resp = UInt(2.W)
  val user = userType.map(_.cloneType).getOrElse(UInt(0.W))
}
