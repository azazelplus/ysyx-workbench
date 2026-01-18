// MiniRV 5级流水线原理图
// 使用 circuiteria 库绘制
// 架构: IF -> ID -> EX -> MEM(LSU) -> WB

#import "@preview/circuiteria:0.2.0": *
#import "@preview/cetz:0.3.2": draw

#set page(width: auto, height: auto, margin: 1cm)
#set text(size: 8pt, font: "DejaVu Sans")

#circuit({
  // =============================================================================
  // 标题
  // =============================================================================
  draw.content(
    (22, 16),
    text(14pt, weight: "bold")[MiniRV 5-Stage Pipeline Architecture],
    anchor: "south"
  )

  // =============================================================================
  // IF 阶段 - IFU (Instruction Fetch Unit)
  // =============================================================================
  element.block(
    x: 0, y: 0, w: 4, h: 6,
    id: "ifu",
    fill: blue.lighten(30%),
    stroke: blue,
    name: "IFU",
    ports: (
      north: (
        (id: "clk", clock: true, small: true),
      ),
      south: (
        (id: "jump_en", name: "jump_en"),
        (id: "jump_addr", name: "jump_addr"),
        (id: "stall", name: "stall"),
      ),
      east: (
        (id: "pc_out", name: "pc"),
        (id: "inst_out", name: "inst"),
      ),
      west: (
        (id: "imem_addr", name: "imem_addr"),
        (id: "imem_rdata", name: "imem_rdata"),
      ),
    ),
    ports-margins: (
      south: (15%, 15%),
      east: (25%, 25%),
      west: (25%, 25%),
    )
  )

  // =============================================================================
  // IF/ID 流水线寄存器
  // =============================================================================
  element.block(
    x: 6, y: 0, w: 1.5, h: 6,
    id: "if_id",
    fill: gray.lighten(30%),
    stroke: gray,
    name: "IF/ID",
    ports: (
      west: (
        (id: "pc_in", name: ""),
        (id: "inst_in", name: ""),
      ),
      east: (
        (id: "pc_out", name: ""),
        (id: "inst_out", name: ""),
      ),
    ),
    ports-margins: (
      west: (25%, 25%),
      east: (25%, 25%),
    )
  )

  // =============================================================================
  // ID 阶段 - IDU + RegFile
  // =============================================================================
  element.block(
    x: 10, y: 0, w: 5, h: 6,
    id: "idu",
    fill: orange.lighten(30%),
    stroke: orange,
    name: "IDU",
    ports: (
      west: (
        (id: "pc_in", name: "pc"),
        (id: "inst_in", name: "inst"),
      ),
      east: (
        (id: "out", name: "ID2EX"),
      ),
      south: (
        (id: "rs1_addr", name: "rs1"),
        (id: "rs2_addr", name: "rs2"),
      ),
      north: (
        (id: "rs1_data", name: "rs1_data"),
        (id: "rs2_data", name: "rs2_data"),
      ),
    ),
    ports-margins: (
      west: (25%, 25%),
      east: (50%, 50%),
      south: (20%, 20%),
      north: (20%, 20%),
    )
  )

  // RegFile
  element.block(
    x: 10, y: 9, w: 5, h: 3,
    id: "regfile",
    fill: purple.lighten(30%),
    stroke: purple,
    name: "RegFile",
    ports: (
      south: (
        (id: "rs1_addr", name: "rs1"),
        (id: "rs2_addr", name: "rs2"),
      ),
      north: (
        (id: "rd_addr", name: "rd"),
        (id: "rd_data", name: "data"),
        (id: "wen", name: "wen"),
      ),
      west: (
        (id: "rs1_data", name: "rs1_data"),
        (id: "rs2_data", name: "rs2_data"),
      ),
    ),
    ports-margins: (
      south: (20%, 20%),
      north: (15%, 15%),
      west: (25%, 25%),
    )
  )

  // =============================================================================
  // ID/EX 流水线寄存器
  // =============================================================================
  element.block(
    x: 17, y: 0, w: 1.5, h: 6,
    id: "id_ex",
    fill: gray.lighten(30%),
    stroke: gray,
    name: "ID/EX",
    ports: (
      west: (
        (id: "in", name: ""),
      ),
      east: (
        (id: "out", name: ""),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      east: (50%, 50%),
    )
  )

  // =============================================================================
  // EX 阶段 - EXU
  // =============================================================================
  element.block(
    x: 21, y: 0, w: 5, h: 6,
    id: "exu",
    fill: yellow.lighten(30%),
    stroke: yellow,
    name: "EXU",
    ports: (
      west: (
        (id: "in", name: "ID2EX"),
      ),
      east: (
        (id: "out", name: "EX2LS"),
      ),
      south: (
        (id: "jump_en", name: "jump_en"),
        (id: "jump_addr", name: "jump_addr"),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      east: (50%, 50%),
      south: (20%, 20%),
    )
  )

  // =============================================================================
  // EX/MEM 流水线寄存器
  // =============================================================================
  element.block(
    x: 28, y: 0, w: 1.5, h: 6,
    id: "ex_mem",
    fill: gray.lighten(30%),
    stroke: gray,
    name: "EX/MEM",
    ports: (
      west: (
        (id: "in", name: ""),
      ),
      east: (
        (id: "out", name: ""),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      east: (50%, 50%),
    )
  )

  // =============================================================================
  // MEM 阶段 - LSU
  // =============================================================================
  element.block(
    x: 32, y: 0, w: 5, h: 6,
    id: "lsu",
    fill: green.lighten(30%),
    stroke: green,
    name: "LSU",
    ports: (
      west: (
        (id: "in", name: "EX2LS"),
      ),
      east: (
        (id: "out", name: "LS2WB"),
      ),
      south: (
        (id: "dmem_req", name: "req"),
        (id: "dmem_resp", name: "resp"),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      east: (50%, 50%),
      south: (25%, 25%),
    )
  )

  // =============================================================================
  // MEM/WB 流水线寄存器
  // =============================================================================
  element.block(
    x: 39, y: 0, w: 1.5, h: 6,
    id: "mem_wb",
    fill: gray.lighten(30%),
    stroke: gray,
    name: "MEM/WB",
    ports: (
      west: (
        (id: "in", name: ""),
      ),
      east: (
        (id: "out", name: ""),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      east: (50%, 50%),
    )
  )

  // =============================================================================
  // WB 阶段 - WBU
  // =============================================================================
  element.block(
    x: 43, y: 0, w: 4, h: 6,
    id: "wbu",
    fill: rgb("ffc0cb"),
    stroke: red,
    name: "WBU",
    ports: (
      west: (
        (id: "in", name: "LS2WB"),
      ),
      north: (
        (id: "rd_addr", name: "rd"),
        (id: "rd_data", name: "data"),
        (id: "wen", name: "wen"),
      ),
    ),
    ports-margins: (
      west: (50%, 50%),
      north: (15%, 15%),
    )
  )

  // =============================================================================
  // PMEM - 物理存储器
  // =============================================================================
  element.block(
    x: -6, y: 0, w: 4, h: 8,
    id: "pmem",
    fill: teal.lighten(30%),
    stroke: teal,
    name: "PMEM",
    ports: (
      east: (
        (id: "imem_addr", name: "i_addr"),
        (id: "imem_rdata", name: "i_data"),
        (id: "dummy", name: ""),
        (id: "dmem_addr", name: "d_addr"),
        (id: "dmem_wdata", name: "d_wdata"),
        (id: "dmem_rdata", name: "d_rdata"),
      ),
    ),
    ports-margins: (
      east: (10%, 10%),
    )
  )

  // =============================================================================
  // HDU - 冒险检测单元
  // =============================================================================
  element.block(
    x: 10, y: -6, w: 6, h: 3,
    id: "hdu",
    fill: red.lighten(30%),
    stroke: red,
    name: "HDU",
    ports: (
      north: (
        (id: "rs1", name: "rs1"),
        (id: "rs2", name: "rs2"),
        (id: "ex_rd", name: "ex_rd"),
        (id: "ex_mem_ren", name: "mem_ren"),
      ),
      east: (
        (id: "stall", name: "stall"),
        (id: "flush", name: "flush"),
      ),
      west: (
        (id: "jump_en", name: "jump_en"),
      ),
    ),
    ports-margins: (
      north: (15%, 15%),
      east: (25%, 25%),
    )
  )

  // =============================================================================
  // FWU - 数据前递单元
  // =============================================================================
  element.block(
    x: 25, y: 9, w: 8, h: 3,
    id: "fwu",
    fill: rgb("00ffff").lighten(30%),
    stroke: rgb("00ffff"),
    name: "FWU (Forwarding Unit)",
    ports: (
      south: (
        (id: "ex_mem_rd", name: "ex_mem_rd"),
        (id: "mem_wb_rd", name: "mem_wb_rd"),
      ),
      north: (
        (id: "rs1_fwd", name: "rs1_fwd"),
        (id: "rs2_fwd", name: "rs2_fwd"),
      ),
      west: (
        (id: "rs1_raw", name: "rs1_raw"),
        (id: "rs2_raw", name: "rs2_raw"),
      ),
    ),
    ports-margins: (
      south: (25%, 25%),
      north: (25%, 25%),
      west: (25%, 25%),
    )
  )

  // =============================================================================
  // 连线: 流水线主干 (Pipeline Datapath)
  // =============================================================================
  
  // IFU -> IF/ID
  wire.wire("w-if-id-pc", ("ifu-port-pc_out", "if_id-port-pc_in"), directed: true)
  wire.wire("w-if-id-inst", ("ifu-port-inst_out", "if_id-port-inst_in"), directed: true)
  
  // IF/ID -> IDU
  wire.wire("w-id-pc", ("if_id-port-pc_out", "idu-port-pc_in"), directed: true)
  wire.wire("w-id-inst", ("if_id-port-inst_out", "idu-port-inst_in"), directed: true)
  
  // IDU -> ID/EX
  wire.wire("w-id-ex", ("idu-port-out", "id_ex-port-in"), directed: true)
  
  // ID/EX -> EXU
  wire.wire("w-ex-in", ("id_ex-port-out", "exu-port-in"), directed: true)
  
  // EXU -> EX/MEM
  wire.wire("w-ex-mem", ("exu-port-out", "ex_mem-port-in"), directed: true)
  
  // EX/MEM -> LSU
  wire.wire("w-mem-in", ("ex_mem-port-out", "lsu-port-in"), directed: true)
  
  // LSU -> MEM/WB
  wire.wire("w-mem-wb", ("lsu-port-out", "mem_wb-port-in"), directed: true)
  
  // MEM/WB -> WBU
  wire.wire("w-wb-in", ("mem_wb-port-out", "wbu-port-in"), directed: true)

  // =============================================================================
  // 连线: IFU <-> PMEM (指令存储)
  // =============================================================================
  wire.wire("w-imem-addr", ("pmem-port-imem_addr", "ifu-port-imem_addr"), directed: true, reverse: true)
  wire.wire("w-imem-data", ("pmem-port-imem_rdata", "ifu-port-imem_rdata"), directed: true)

  // =============================================================================
  // 连线: LSU <-> PMEM (数据存储)
  // =============================================================================
  wire.wire("w-dmem-req", ("lsu-port-dmem_req", "pmem-port-dmem_addr"), 
    style: "zigzag", zigzag-dir: "vertical", zigzag-ratio: 10%, directed: true)
  wire.wire("w-dmem-resp", ("pmem-port-dmem_rdata", "lsu-port-dmem_resp"), 
    style: "zigzag", zigzag-dir: "vertical", zigzag-ratio: 90%, directed: true)

  // =============================================================================
  // 连线: RegFile
  // =============================================================================
  // IDU rs1/rs2 addr -> RegFile
  wire.wire("w-rs1-addr", ("idu-port-rs1_addr", "regfile-port-rs1_addr"), directed: true)
  wire.wire("w-rs2-addr", ("idu-port-rs2_addr", "regfile-port-rs2_addr"), directed: true)

  // =============================================================================
  // WBU -> RegFile 写回连线 (Write Back)
  // =============================================================================
  wire.wire(
    "w-wb-rd",
    ("wbu-port-rd_addr", "regfile-port-rd_addr"),
    style: "zigzag",
    zigzag-dir: "vertical",
    zigzag-ratio: 10%,
    directed: true
  )
  wire.wire(
    "w-wb-data",
    ("wbu-port-rd_data", "regfile-port-rd_data"),
    style: "zigzag",
    zigzag-dir: "vertical",
    zigzag-ratio: 20%,
    directed: true
  )
  wire.wire(
    "w-wb-wen",
    ("wbu-port-wen", "regfile-port-wen"),
    style: "zigzag",
    zigzag-dir: "vertical",
    zigzag-ratio: 30%,
    directed: true
  )

  // =============================================================================
  // EXU -> IFU 跳转信号 (Branch/Jump)
  // =============================================================================
  wire.wire(
    "w-jump-en",
    ("exu-port-jump_en", "ifu-port-jump_en"),
    style: "zigzag",
    zigzag-dir: "vertical",
    zigzag-ratio: 80%,
    //paint: color.orange, 
    directed: true
  )
  wire.wire(
    "w-jump-addr",
    ("exu-port-jump_addr", "ifu-port-jump_addr"),
    style: "zigzag",
    zigzag-dir: "vertical",
    zigzag-ratio: 90%,
    directed: true
  )

  // =============================================================================
  // HDU 连线 (Hazard Detection)
  // =============================================================================
  // Inputs
  wire.wire("w-hdu-rs1", ("idu-port-rs1_addr", "hdu-port-rs1"), style: "zigzag", directed: true)
  wire.wire("w-hdu-rs2", ("idu-port-rs2_addr", "hdu-port-rs2"), style: "zigzag", directed: true)
  wire.wire("w-hdu-jump", ("exu-port-jump_en", "hdu-port-jump_en"), style: "zigzag", directed: true)
  
  // Outputs (Stall/Flush)
  wire.wire(
    "w-stall",
    ("hdu-port-stall", "ifu-port-stall"),
    style: "zigzag",
    zigzag-dir: "horizontal",
    zigzag-ratio: 50%,
    directed: true
  )
  
  // =============================================================================
  // FWU 连线 (Forwarding)
  // =============================================================================
  // Inputs
  wire.wire("w-fwu-rs1", ("idu-port-rs1_addr", "fwu-port-rs1_raw"), style: "zigzag", directed: true)
  wire.wire("w-fwu-rs2", ("idu-port-rs2_addr", "fwu-port-rs2_raw"), style: "zigzag", directed: true)
  
  // Outputs
  wire.wire("w-fwd-rs1", ("fwu-port-rs1_fwd", "idu-port-rs1_data"), style: "zigzag", directed: true)
  wire.wire("w-fwd-rs2", ("fwu-port-rs2_fwd", "idu-port-rs2_data"), style: "zigzag", directed: true)

  // =============================================================================
  // 阶段标签
  // =============================================================================
  draw.content((2, -2), text(12pt, weight: "bold")[IF], anchor: "north")
  draw.content((10, -2), text(12pt, weight: "bold")[ID], anchor: "north")
  draw.content((21, -2), text(12pt, weight: "bold")[EX], anchor: "north")
  draw.content((32, -2), text(12pt, weight: "bold")[MEM], anchor: "north")
  draw.content((43, -2), text(12pt, weight: "bold")[WB], anchor: "north")

  // HDU/FWU 标签
  draw.content((10, -8), [Hazard Detection Unit], anchor: "north")
  draw.content((29, 11), [Forwarding Unit], anchor: "north")
})
