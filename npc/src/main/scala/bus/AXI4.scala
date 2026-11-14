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

// AXI4 五个通道的信号bundle类
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


/*note

scala中, 类的构造参数前面可以通过加关键词`val` 来使这个构造参数被暴露为该类的一个字段(成员变量的java说法). 
以一个简单的二维坐标类为例. 在scala中这样一行干了cpp中一大坨的事:

```scala
  class Point(val x: Int, val y: Int)
```

```cpp
class Point {
public:
    int x;    // 成员
    int y;    // 成员
    Point(int x_, int y_) : x(x_), y(y_) {}
};
```

本代码中的userType: Option[Data] = None   //userType是一个可选构造参数. 前面不加val, 所以不作为字段暴露出去, 只是作为构造参数传入.  Option[T]是scala用来表示可能有值, 也可能没值的容器. = None是默认值.

这是因为, AXI4规定, 每个通道都有一个可选的`USER`信号, 用来让总线使用者携带协议外自定义信息, 如:

| 常用自定义信号 | 所属通道 | 规范中的作用 |
| --- | --- | --- |
| **`lock`** | AW/AR | 原子访问/独占访问控制 |
| **`cache`** | AW/AR | 内存类型（如Bufferable, Cacheable） |
| **`prot`** | AW/AR | 保护属性（特权级、安全、指令/数据） |
| **`qos`** | AW/AR | 服务质量（Quality of Service） |
| **`region`** | AW/AR | 地址区域指示（用于多区域从机） |
| **`atomic`** | AW  | 原子操作类型（如Swap, Compare） |

userType: Option[Data] = None 是为了后续开发留口子...


*/






























