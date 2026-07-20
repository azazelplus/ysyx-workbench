// bus的参数配置。
package bus

import chisel3._
import chisel3.util._

/** AXI4 总线宽度参数 — 所有包共享的唯一宽度定义源。
  *
  * AXI4 协议固定字段（len 8-bit, size 3-bit, burst 2-bit, resp 2-bit）不在此处参数化。
  */
case class BusConfig(
    addrBits: Int,
    dataBits: Int,
    idBits: Int
) {
  require(addrBits > 0, "addrBits must be > 0")
  require(
    dataBits > 0 && (dataBits & (dataBits - 1)) == 0,
    "dataBits must be power of 2"
  )
  require(idBits > 0, "idBits must be > 0")

  val dataBytes: Int = dataBits / 8
  val strbBits: Int = dataBytes

  // Chisel Width 便捷字段
  val addrWidth: Width = addrBits.W
  val dataWidth: Width = dataBits.W
  val idWidth: Width = idBits.W
  val strbWidth: Width = strbBits.W
}
