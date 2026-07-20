package device

import chisel3._
import chisel3.util._
import bus._

// debug
import chisel3.dontTouch

/*   
* 片上资源模拟 RAM 模块 的存储器. 简单双端口.
* 时序特性: 异步读 + 同步写 + 写转发保护（同周期 RAW(虽然这正常情况下应该不会发生) , 返回最新数据）
* 核心是`mem`, 用Mem()实例化出一个异步读同步写(这个读写逻辑是用户代码决定的)的硬件. Mem会被vivado综合为BRAM. BRAM硬件特性: 异步读同步写, 同一时刻读写同地址则返回旧值.
* Mem()对象的.read方法是异步读, .write方法是同步写. 所以chisel设计上就希望调用这两个方法实现异步读(节省时钟周期)同步写(暴政存储内容不被噪声破坏)的存储器行为.
*/
class AsyncMemModule(val depth: Int = 1024, val dataWidth: Int = 32)
    extends Module {
  val maskWidth = dataWidth / 8 //掩码宽度(byte) 
  val io = IO(new Bundle {
    // 读端口
    val readEnable = Input(Bool())  //读使能
    val readAddr = Input(UInt(log2Ceil(depth).W)) //读地址
    val readData = Output(UInt(dataWidth.W))  //读数据

    // 写端口
    val writeEnable = Input(Bool())
    val writeAddr = Input(UInt(log2Ceil(depth).W))
    val writeData = Input(UInt(dataWidth.W))    // 写数据
    val writeStrb = Input(UInt(maskWidth.W))    // 写掩码
  })

  // 使用 Mem 实例化异步读、同步写的存储器，按字节划分以支持写入掩码.
  val mem = Mem(depth, Vec(maskWidth, UInt(8.W)))

  // -------------------------
  // Mem对象的写逻辑：同步写（时序逻辑）
  // -------------------------
  val writeDataVec = VecInit(
    Seq.tabulate(maskWidth)(i => io.writeData(8 * i + 7, 8 * i)) // writeDataVec: 将io.writeData(32bit)按字节切分成一个掩码(即8bit)宽度的(4*8bit)向量.   `Seq.tabulate(N){i => {exp} }` 这个表达式返回一个N个元素的序列. 其中每个元素是{exp}这个表达式的返回值. 对每个元素, {exp}中的遍历变量i被替换为0,1,...,N-1.
  )
  

  when(io.writeEnable) {mem.write(io.writeAddr, writeDataVec, io.writeStrb.asBools)}   // 同步写. Mem()对象的.write方法实现同步写. 对应签名: `def write(addr: UInt, data: T, mask: Seq[Bool]): Unit`  





  // -------------------------
  // Mem的读逻辑：异步读 
  // -------------------------
  val rawReadData = mem.read(io.readAddr) //异步读. `.read`方法即异步读(Combinatorial Read). 生成组合逻辑连线.

  // -------------------------
  // Mem写转发保护防御性设计...（同周期 RAW 返回最新数据）
  // Mem 异步读在同周期写时返回旧值，需按字节 strobe 转发写数据
  // -------------------------
  val sameAddrHazard = io.writeEnable && io.readEnable && (io.writeAddr === io.readAddr)  // RAW信号. 
  // mergedData是最终的有效读数据, 复选器根据sameAddrHazard选择每个字节是来自rawReadData(正常读出)还是来自writeDataVec(写转发).
  val mergedData = VecInit(Seq.tabulate(maskWidth) { i =>
    Mux(
      sameAddrHazard && io.writeStrb(i),
      io.writeData(8 * i + 7, 8 * i),
      rawReadData(i)
    )
  })
  io.readData := Mux(io.readEnable, mergedData.asUInt, 0.U) //读使能关闭时, 读出0. 读使能打开时, 读出mergedData.
}



/** 片上模拟RAM – 支持 AXI4 突发传输的可参数化延迟 SRAM 模型. io就是一个AXI4SlaveBus接口. 
 * 就是AsyncMemModule的AXI协议包裹层 + 模拟延迟设计.
 */
class RAM(implicit val cfg: RAMConfig) extends Module {

  val io = IO(new Bundle {
    val axi = new AXI4SlaveBus(cfg.bus)
  })

  //实例化一个存储核心AsyncMemModule
  val mem = Module(
    new AsyncMemModule(cfg.memorySizeInWords, cfg.bus.dataBits)
  )

  // 初始化axi接口
  io.axi.init()               // init是Chisel Bundle 的初始化方法, 将所有信号初始为默认值.
  io.axi.r.bits.resp := 0.U   // 读响应设为 OKAY
  io.axi.b.bits.resp := 0.U   // 写响应设为 OKAY

  // =========================================================
  // 读事务 AR→R 转换状态机.  INCR(Increment) Burst，流水化 outstanding
  // 每个请求独立 readLatency 倒计时，多个请求延迟并行重叠
  // R 通道不交织.同一时刻只服务一个突发的 beat.
  // =========================================================
  private val N = cfg.outstanding               // 设置的最大未完成事务数N, 即索引槽数
  private val rIdxBits = log2Ceil(N).max(1)     // 索引槽编码位数.
  private val rDelayBits = log2Ceil(cfg.readLatency).max(1) //倒计时延迟编码位数.

  // 读请求表. 本质是 AR 申请寄存器组, 记录还没完成对应R事务的AR事务情况. N组寄存器, 存储每个槽位的信息...每个槽位独立跟踪一笔 AR 请求
  val rEntryValid = RegInit(VecInit(Seq.fill(N)(false.B)))  // 槽位的使能寄存器(默认0). 为0表示空闲.  1 * N bits
  val rEntryId = Reg(Vec(N, UInt(cfg.bus.idBits.W)))        // 本槽位存的AR事务ID. bus.idBits * N bits
  val rEntryAddr = Reg(Vec(N, UInt(cfg.memWordAddrBits.W))) // AR事务目标读取地址
  val rEntryLen = Reg(Vec(N, UInt(8.W)))                  // 即AXI中的ARLEN, 即`突发beat数-1`
  val rEntryBeat = Reg(Vec(N, UInt(8.W)))                 // 当前 Beat 索引, 用于跟踪当前burst的进度. rEntryBeat == rEntryLen 时, 触发R通道的 RLAST信号拉高, 本次突发传输完成.
  val rEntryDelay = Reg(Vec(N, UInt(rDelayBits.W)))       // 本AR事务的剩余延迟周期数.

  // AR 接收：找到空闲槽位即可接受
  val rFreeVec = VecInit(rEntryValid.map(!_))   //rFreeVec这个寄存器组(槽位空闲寄存器)仅仅是rEntryValid的取反.    `.map`: 对集合里所有元素做相同操作.  (!_)是匿名函数简写, 总之等价于匿名函数`x=>!x`, 给所有元素取反.
  val rHasFree = rFreeVec.asUInt.orR        //  `.asUInt`把vec[bool]转成一个按位拼起来的UInt. 比如Vec(false, true, false, false).asUInt === b0100.U
  val rFreeIdx = PriorityEncoder(rFreeVec)  // 用优先编码器找到第一个为1的index. rFreeIdx=0则说明此时没有空闲槽位.

  io.axi.ar.ready := rHasFree // AR通道ready信号即为是否有空闲槽位. 只要有, AR就准备好接受新请求.
  // 读请求表的寄存器更新逻辑: AR fire时更新. (接收了新的AR请求, 从而产生一个新的代办R事务, 占用一个新的槽位.)
  when(io.axi.ar.fire) {
    rEntryValid(rFreeIdx) := true.B   //该槽位被占用
    rEntryId(rFreeIdx) := io.axi.ar.bits.id // 记录AR事务ID
    rEntryAddr(rFreeIdx) := io.axi.ar.bits.addr(cfg.memWordAddrBits + 1, 2) // 记录AR事务地址. AXI地址是字节地址, 而RAM按字(32bit)寻址, 所以要去掉最低两位.
    rEntryLen(rFreeIdx) := io.axi.ar.bits.len // 记录AR事务的突发长度(beat数-1)
    rEntryBeat(rFreeIdx) := 0.U   // 新事务从第0个beat开始
    rEntryDelay(rFreeIdx) := (cfg.readLatency - 1).U // 记录AR事务的初始延迟周期数. 
  }

  // 所有有效槽位并行倒计时.
  for (i <- 0 until N) {
    when(rEntryValid(i) && rEntryDelay(i) > 0.U) {
      rEntryDelay(i) := rEntryDelay(i) - 1.U
    }
  }

  // R输出仲裁ready信号: 不交织，锁定到一个突发直到 last
  val rReadyVec = VecInit(
    (0 until N).map(i => rEntryValid(i) && rEntryDelay(i) === 0.U)
  )   // rReadyVec: 每个槽位有没有准备好R 输出. (有效&&延迟倒计时为0时准备好)
  val rHasReady = rReadyVec.asUInt.orR      // 当前是否有一个槽位R ready.
  val rNextIdx = PriorityEncoder(rReadyVec) // 这个准备好R ready的槽位id.


  //  burst lock逻辑
  val rSending = RegInit(false.B)       // rSending寄存器用来存储当前是否正在R输出中.
  val rSendIdx = Reg(UInt(rIdxBits.W))  // rSendIdx寄存器用来存储当前正在R输出的槽位id.
  val rActiveIdx = Mux(rSending, rSendIdx, rNextIdx)  // (如果能)该用哪个槽位R输出? 如果正在R输出, 当然用rSendIdx, 如果不在输出, 则应当用rNextIdx
  val rActiveValid = Mux(rSending, true.B, rHasReady) // R通道总开关(即axi.r.valid) 现在能不能R输出? 如果正在输出burst过程中, 当然可以. 如果不在输出, 则要等rHasReady(有一个槽位准备好R了)

  //R输出状态机
  when(rActiveValid && !rSending) { //当R通道准备好输出 && 当前不在输出中: 意味着一个新的burst将要开始.
    rSending := true.B  // R输出状态: 正在R输出.
    rSendIdx := rActiveIdx  // 记录当前R输出槽位id
  }

  when(io.axi.r.fire) {   // R通道fire时处理：同时更新burst lock状态机和读请求槽位
    when(rEntryBeat(rActiveIdx) === rEntryLen(rActiveIdx)) {// 当前beat是突发的最后一个：槽位清理 + burst lock解锁
      rEntryValid(rActiveIdx) := false.B                        // 释放读请求槽位
      rSending := false.B                                       // 解锁burst lock，下周期可服务新突发
    }.otherwise {         // 当前beat不是最后一个：继续迭代
      rEntryBeat(rActiveIdx) := rEntryBeat(rActiveIdx) + 1.U    // beat计数递增
      rEntryAddr(rActiveIdx) := rEntryAddr(rActiveIdx) + 1.U    // 地址指向下一字
    }
  }

  // 连接 读通道状态机产生的协议控制信号 到 内部实例化的存储核心模块AsyncMemModule
  mem.io.readEnable := rActiveValid     // 存储核心模块AsyncMemModule的读使能信号, 由协议状态机产生的rActiveValid (R通道ready) 驱动.
  mem.io.readAddr := rEntryAddr(rActiveIdx)   // 存储核心模块AsyncMemModule的读地址信号, 由协议状态机产生的rEntryAddr(rActiveIdx) 驱动. target

  // 连接 读通道状态机产生的协议控制信号 到 AXI4SlaveBus接口的R通道输出
  io.axi.r.valid := rActiveValid
  io.axi.r.bits.data := mem.io.readData
  io.axi.r.bits.last := rEntryBeat(rActiveIdx) === rEntryLen(rActiveIdx)
  io.axi.r.bits.id := rEntryId(rActiveIdx)


  // =========================================================
  // 写事务状态机. 由AW请求缓冲队列 + W通道FSM + B响应延迟流水线组成.
  // 支持 INCR 突发，流水化 outstanding. W 数据因单写端口仍串行消耗，但 B 延迟与下一事务 W 接收并行重叠
  // 当前, 写RAM的唯一上游是DCache.
  // =========================================================


  // =============================================
  // ── AW 缓冲队列, 深度为N. ──
  // =============================================
  val awQueue = Module(
    new Queue(new AXI4AW(cfg.bus), N, pipe = true, flow = true)
  )
  awQueue.io.enq <> io.axi.aw  // 队列连接到上游Master, 即axi.aw, 从上游接收AW和W.
  // awQueue的下游出队口连接到下面的写数据FSM.



  // =============================================
  // ── B 响应延迟流水线, 深度为N(读), 环形FIFO ──
  // =============================================
  private val wDelayBits = log2Ceil(cfg.writeLatency).max(1)  // 写延迟计数器位宽
  val bPipeValid = RegInit(VecInit(Seq.fill(N)(false.B)))     // 槽位有效标志
  val bPipeId = Reg(Vec(N, UInt(cfg.bus.idBits.W)))           // B响应的ID寄存器, 存储本槽位B响应来自哪个写事务ID.
  val bPipeDelay = Reg(Vec(N, UInt(wDelayBits.W)))            // B响应的延迟寄存器. 每个槽位独立计时.

  // 环形 FIFO 指针部分
  val bPipeEnq = RegInit(0.U(rIdxBits.W)) // B响应入队指针, 指向队尾
  val bPipeDeq = RegInit(0.U(rIdxBits.W)) // B响应出队指针, 指向队头
  val bPipeMaybeFull = RegInit(false.B) // 这是一个记忆信号, 当本周期: 只入队(队趋向满)时, 置一. 只出队(队趋向空)时, 置零. 同时出入队/不出入队(队长度不变)时, 不变. 
  val bPipeFull = (bPipeEnq === bPipeDeq) && bPipeMaybeFull // 环形FIFO满标志: 入队出队指针相等 && 最近几次操作是: 只入队+ n次同时出入队.

  // bPipeDelay寄存器 并行倒计时
  for (i <- 0 until N) {
    when(bPipeValid(i) && bPipeDelay(i) > 0.U) {  //
      bPipeDelay(i) := bPipeDelay(i) - 1.U
    }
  }

  // B 输出：队头就绪（delay=0）时输出 + 出队
  val bHeadReady = bPipeValid(bPipeDeq) && bPipeDelay(bPipeDeq) === 0.U // 队头就绪
  io.axi.b.valid := bHeadReady            // B通道有效信号由队头就绪驱动
  io.axi.b.bits.id := bPipeId(bPipeDeq)   // 接入b通道 事务ID 
  // axi.b.bits.resp一直是初始化0, 因为模拟RAM不会出错.
  // axi.b.bits.user也用不到, 一直是初始化值.

  val doDeqB = io.axi.b.fire  // b.fire即出队信号.
  when(doDeqB) {
    bPipeValid(bPipeDeq) := false.B
    bPipeDeq := Mux(bPipeDeq === (N - 1).U, 0.U, bPipeDeq + 1.U)
  }

  // B 入队
  val doEnqB = WireDefault(false.B) // 入队信号. 由下方 写数据FSM 产生.
  val bEnqId = Wire(UInt(cfg.bus.idBits.W))
  bEnqId := 0.U
  when(doEnqB) {
    bPipeValid(bPipeEnq) := true.B
    bPipeId(bPipeEnq) := bEnqId
    bPipeDelay(bPipeEnq) := (cfg.writeLatency - 1).U
    bPipeEnq := Mux(bPipeEnq === (N - 1).U, 0.U, bPipeEnq + 1.U)
  }
  when(doEnqB =/= doDeqB) {
    bPipeMaybeFull := doEnqB  // 当 
  }



  // =============================================
  // ── W阶段 简陋 FSM ──
  // 只使用AW请求的addr和id字段. 
  // 无视len(无视突发长度. 信任master(Dcache)给出的W通道w.bit.last时序正确.), size(认为所有数据都是合法的字(32bit)), burst(认为burst一定是INCR模式), user.
  // =============================================
  val wIdle :: wRecv :: Nil = Enum(2) // 2状态: wIdle(空闲), wRecv(接收中)
  val wState = RegInit(wIdle) // 状态机状态寄存器
  val wAddr = Reg(UInt(cfg.memWordAddrBits.W))  // 写地址指针(按字寻址). 需要这个指针寄存器是因为burst时写地址会递增.
  val wIdReg = Reg(UInt(cfg.bus.idBits.W))  // 写事务ID寄存器.

  awQueue.io.deq.ready := false.B
  io.axi.w.ready := false.B

  mem.io.writeAddr := wAddr
  mem.io.writeData := io.axi.w.bits.data
  
  // DEBUG信号2: RAM的io.axi.w.bits.strb. 它来自Dcache的AXI MASTER信号. 问题所在!!!!!
  // val dbg_axi_w_strb = Wire(UInt(cfg.bus.strbBits.W))
  // dbg_axi_w_strb := io.axi.w.bits.strb
  // dontTouch(dbg_axi_w_strb)
  // mem.io.writeStrb := dbg_axi_w_strb
  mem.io.writeStrb := io.axi.w.bits.strb
  
  mem.io.writeEnable := false.B

  switch(wState) {
    // wIdle状态: 空闲, 等待接收新写事务.
    is(wIdle) {
      // 当AW队列有出队请求且B响应队列不满时, 接收新写事务.
      when(awQueue.io.deq.valid && !bPipeFull) {  
        awQueue.io.deq.ready := true.B    // AW队列准备好出队
        wIdReg := awQueue.io.deq.bits.id  // 记录写事务ID
        io.axi.w.ready := true.B          // RAM的axi接口准备好接受w数据
        // 如果w数据在同周期到达, 则直接处理第一个w数据. 这种情况在Master一次性发出AW和W时会发生.
        when(io.axi.w.fire) { 
          mem.io.writeAddr := awQueue.io.deq.bits.addr(cfg.memWordAddrBits + 1, 2) // AW缓冲队列 出队 AW请求的addr字段, 给mem.
          mem.io.writeEnable := true.B  //置高核心存储模块mem的写使能, 触发写入
          wAddr := awQueue.io.deq.bits.addr(cfg.memWordAddrBits + 1, 2) + 1.U // 写地址指针递增, 指向下一个data.
          // 如果是单beat写事务, 本周期last就拉高, 写事务完成, 准备发送B事务.
          when(io.axi.w.bits.last) {  
            doEnqB := true.B  // 拉高B事务入队信号
            bEnqId := awQueue.io.deq.bits.id  // 准备B事务data(只有写事务id)
          }
          // 对单beat写事务, 一周期处理完, W状态机仍然呆在wIdle, 等待处接收下一个写事务.
          .otherwise { // 如果不是最后一个beat, 继续接收burst的下一个beat.
            wState := wRecv // 写状态机切换到接收中状态.
          }
        }
        // AW到达, 但是本周期w数据没有同步送达时:
        .otherwise {
          wAddr := awQueue.io.deq.bits.addr(cfg.memWordAddrBits + 1, 2) // AW缓冲队列 出队 AW请求的addr字段, 给mem.(此时mem.io.writeAddr信号线一直维持信息, 但是mem没有使能, 所以用不到)
          wState := wRecv // W状态机切换到`接收中`状态, 等待w数据到达.
        }
      }
    }
    // 接收中: 上一周期正在传输未结束的burst beat, 本周期要继续传输. 或者已有AW请求进入W状态机, 但是w信息没到需要等.
    is(wRecv) {
      io.axi.w.ready := true.B  // RAM的W通道准备好接收w数据.
      // W通道接收一个beat时: 打开mem使能允许本周期写入一个beat.
      when(io.axi.w.fire) {
        mem.io.writeEnable := true.B  // 打开存储模块写使能
        wAddr := wAddr + 1.U  // W状态机写地址指针递增, 指向下一个data. 
        // 如果这是突发的最后一个beat, 则写事务完成, 准备发送B事务, 状态机回到wIdle等待下一个写事务.
        when(io.axi.w.bits.last) {
          doEnqB := true.B
          bEnqId := wIdReg
          wState := wIdle
        }
      }
    }
  }
}

// notes
// DecoupledIO 是从生产者的角度定义的: 一个模块的DecoupledIO,
  // valid方向是output, 期望接入下游模块
  // bits方向是output, 期望接入下游模块
  // ready方向是input, 期望接入上游模块
  // fire条件是 valid && ready. (注意, 当写io.enq.fire时)

// Queue是Chisel标准库的深度为N的FIFO队列. 由Reg组和组合逻辑控制组成. 会被vivado尝试综合为分布式RAM(深度小), BRAM(深度大). 
// Queue参数: Queue(数据类型(bundle类), 深度, pipe(管道标志, 是否允许满队列在一个周期同时入列和出列) = 1/0, flow(穿透标志: 允许在队列空时, 输入数据直接组合逻辑输出) = 1/0). 
// Queue对象对外连接: 只需要用<>连接两个接口.
/*  
* io.enq 类型是 Flipped(DecoupledIO(gen bundle)) , 入队口, 接入上游模块的 DecoupledIO.
* io.deq 类型是 DecoupledIO(gen bundle), 出对口, 接入下游模块的 Flipped(DecoupledIO).
* io.enq.ready由Queue对象根据内部状态自动生成: 队列满时ready=0, 队列不满时ready=1. 
* io.deq.valid由Queue对象根据内部状态自动生成: 队列空时valid=0, 队列非空时valid=1.
*/