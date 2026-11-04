// Crossbar. 未实现
package azazerv

import chisel3._
import chisel3.util._


// xbar的核心功能是将请求的地址转换为下游的编号.
class Xbar extends Module {
    val io = IO(new Bundle {
        val addr = Input(UInt(32.W)) // 请求地址
        val resp = Output(UInt(2.W)) // 返回值
    })





}