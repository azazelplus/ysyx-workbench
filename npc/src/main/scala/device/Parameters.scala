// device的参数配置
package device

import chisel3.util._
import bus.BusConfig

/** RAM(片上资源模拟) 配置 */
case class RAMConfig(
    bus: BusConfig,   //总线参数
    memorySizeBytes: Long,  //RAM容量，单位Byte
    readLatency: Int,   //读延迟，单位周期
    writeLatency: Int,  //写延迟，单位周期
    outstanding: Int    // 最大未完成读事务数/写事务数. (简便起见都设为同一个值)
) {
  require(readLatency >= 1, "readLatency must be >= 1")
  require(writeLatency >= 1, "writeLatency must be >= 1")

  val dataBytes: Int = bus.dataBytes  // 即一个data(也称word)有多少bytes. 32位即4.
  val memorySizeInWords: Int = (memorySizeBytes / dataBytes).toInt  // RAM容量换算成以data(word)为单位的地址空间大小. 
  val memWordAddrBits: Int = log2Ceil(memorySizeInWords)  // RAM地址(按data(word, 字)为单位!!)空间的位宽.
}

/** ROM 专属配置 */
case class ROMConfig(
    bus: BusConfig,
    readLatency: Int,
    outstanding: Int
) {
  require(readLatency >= 1, "readLatency must be >= 1")
}

/** UART 配置 */
case class UARTConfig(
    bus: BusConfig
)

/** Timer 配置 */
case class TimerConfig(
    bus: BusConfig,
    freqMHz: Int
)
