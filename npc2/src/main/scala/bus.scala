package minirv

import chisel3._
import chisel3.util._

/**
  * AXI-Lite 总线接口定义
  *
  * 标准 AXI-Lite 接口，支持 32 位地址和数据宽度
  * 包含 5 个通道: AR (读地址), R (读数据), AW (写地址), W (写数据), B (写响应)
  */

// ========== AR Channel (Read Address) ==========
class AXILiteAR extends Bundle {
  val addr = UInt(Config.ADDR_WIDTH.W)  // 读地址
}

// ========== R Channel (Read Data) ==========
class AXILiteR extends Bundle {
  val data = UInt(Config.XLEN.W)        // 读数据
  val resp = UInt(2.W)                  // 响应: 0=OK, 1=EXOKAY, 2=SLVERR, 3=DECERR
}

// ========== AW Channel (Write Address) ==========
class AXILiteAW extends Bundle {
  val addr = UInt(Config.ADDR_WIDTH.W)  // 写地址
}

// ========== W Channel (Write Data) ==========
class AXILiteW extends Bundle {
  val data = UInt(Config.XLEN.W)        // 写数据
  val strb = UInt(4.W)                  // 字节写使能掩码 (每位对应一个字节)
}

// ========== B Channel (Write Response) ==========
class AXILiteB extends Bundle {
  val resp = UInt(2.W)                  // 响应: 0=OK, 1=EXOKAY, 2=SLVERR, 3=DECERR
}

/**
  * AXI-Lite Master 接口 (从 Master 视角定义)
  *
  * 用于 CPU 的取指单元 (IFU) 和访存单元 (LSU)
  * - ar/aw/w: Master 发送地址和数据到 Slave (Decoupled)
  * - r/b: Master 接收来自 Slave 的响应 (Flipped Decoupled)
  */
class AXILiteMasterIO extends Bundle {
  val ar = Decoupled(new AXILiteAR)           // 读地址通道 (Master → Slave)
  val r  = Flipped(Decoupled(new AXILiteR))   // 读数据通道 (Slave → Master)
  val aw = Decoupled(new AXILiteAW)           // 写地址通道 (Master → Slave)
  val w  = Decoupled(new AXILiteW)            // 写数据通道 (Master → Slave)
  val b  = Flipped(Decoupled(new AXILiteB))   // 写响应通道 (Slave → Master)
}

/**
  * AXI-Lite Slave 接口 (从 Slave 视角定义, 全部 Flipped)
  *
  * 用于物理内存 (PMEM) 或外设
  * - ar/aw/w: Slave 接收来自 Master 的地址和数据 (Flipped Decoupled)
  * - r/b: Slave 发送响应到 Master (Decoupled)
  */
class AXILiteSlaveIO extends Bundle {
  val ar = Flipped(Decoupled(new AXILiteAR))  // 读地址通道 (Master → Slave)
  val r  = Decoupled(new AXILiteR)            // 读数据通道 (Slave → Master)
  val aw = Flipped(Decoupled(new AXILiteAW))  // 写地址通道 (Master → Slave)
  val w  = Flipped(Decoupled(new AXILiteW))   // 写数据通道 (Master → Slave)
  val b  = Decoupled(new AXILiteB)            // 写响应通道 (Slave → Master)
}
