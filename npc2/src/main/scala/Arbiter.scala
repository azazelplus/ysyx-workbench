// 仲裁器. 未实现
package minirv

import chisel3._
import chisel3.util._


//仲裁器是一个状态机!
//调度: 选择一个正在发送有效请求的master
//阻塞: 阻塞其他master的访问
//转发: 将获得访问权的master的请求转发给slave, 并在slave的请求到达时, 将其转发给之前的master
class Arbiter extends Module {
    val io = IO(new Bundle {
        val req = Input(Vec(2, Bool())) // 请求信号，支持 2 个请求者
        val grant = Output(Vec(2, Bool())) // 授权信号，指示哪个请求者被授权访问
    })


    val state = RegInit(0.U(2.W)) // 状态寄存器，初始状态为 0


}


















