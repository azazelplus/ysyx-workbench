// 用于测试 Scala 语法的临时文件
// 运行命令：./mill minirv.runMain hello.Hello
package hello

object Hello extends App {
  println("=== 测试 UInt() 调用链 ===")
  
  import chisel3._
  
  // 测试 1：理解 .W 隐式转换
  val width1 = 4.W
  println(s"4.W 的类型: ${width1.getClass.getName}")
  println(s"4.W 的值: $width1")
  
  // 测试 2：直接创建 UInt
  val uint1 = UInt(4.W)
  println(s"\nUInt(4.W) 的类型: ${uint1.getClass.getName}")
  println(s"UInt(4.W) 的宽度: ${uint1.getWidth}")
  
  // 测试 3：创建带字面量的 UInt
  val uint2 = 5.U(4.W)  // 值为 5，宽度为 4
  println(s"\n5.U(4.W) 的类型: ${uint2.getClass.getName}")
  
  // 测试 4：不同的 Width 创建方式
  import chisel3.util._
  val w1 = Width(8)
  val w2 = 8.W
  println(s"\nWidth(8) == 8.W? ${w1 == w2}")
  
  println("\n=== 调用链总结 ===")
  println("UInt(4.W) 过程:")
  println("1. 4.W → 隐式转换 → Width(4)")
  println("2. UInt(Width(4)) → UInt.apply(width: Width)")
  println("3. UIntFactory.apply → new UInt(Width(4))")
  println("4. 返回一个 4 位宽的 UInt 对象")
  
  println("\n=== 测试完成 ===")
}
