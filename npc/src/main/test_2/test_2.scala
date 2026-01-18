// 用于测试 Scala 语法的临时文件
// 运行命令：./mill minirv.runMain hello.Hello
package test_2

import chisel3._
import chisel3.util._
import minirv._


object Hello extends App {
    println("=== 测试 ===")
  
    class MyModule extends Module {
    val io = IO(new Bundle {
        val in  = Input(UInt(4.W))
        val out = Output(UInt(4.W))
    })
    } 

    val myProcessor = new MyObj() 
    val testData = "azazel"
    myProcessor.runtest(testData)

  
    println("=== 测试完成 ===")
}







class Base {
    def action(data: String): String = {
        println(s"Base 接收到最终数据: $data")
        data //返回值
    }
}


trait LayerA extends Base{
    override def action(data: String): String = {
        val modifiedData = s"[LayerA修饰]$data"
        println(s"LayerA 修饰后返回数据: $modifiedData")
        //调用super, 将控制权交给线性化链的下一个trait.
        super.action(modifiedData)
    }
}

trait LayerB extends Base {
    override def action(data: String): String = {
        val modifiedData = s"[LayerB修饰]$data"
        println(s"LayerB 修饰后返回数据: $modifiedData")
        //调用super, 将控制权交给线性化链的下一个trait.
        super.action(modifiedData)
    }
}


// 线性化顺序（从右到左）：MyObj -> LayerB -> LayerA -> Base
class MyObj extends Base with LayerA with LayerB{

    //默认的线性化调用:
    def runtest(input: String): Unit = {


        println("--- 运行默认调用链: MyObj.action ---")
        //直接调用action(), 则此时调用的是LayerB的action()
        action(input)
        println("------------------------------------\n")

        println("--- 运行默认调用链(效果同上): super.action ---")    
        super.action(input)  
        println("------------------------------------\n")

        println("--- 运行精确跳转: super[LayerA].action ---")    
        super[LayerA].action(input)  
        println("------------------------------------\n")

        println("--- 运行精确跳转: super[Base].action ---")    
        super[Base].action(input)  
        println("------------------------------------\n")


        println("------------------------------------\n")
    }
}

// 预期结果:
// [68] === 测试 ===
// [68] --- 运行默认调用链: MyObj.action ---
// [68] LayerB 修饰后返回数据: [LayerB修饰]azazel
// [68] LayerA 修饰后返回数据: [LayerA修饰][LayerB修饰]azazel
// [68] Base 接收到最终数据: [LayerA修饰][LayerB修饰]azazel
// [68] ------------------------------------
// [68] 
// [68] --- 运行精确跳转: super.action ---
// [68] LayerB 修饰后返回数据: [LayerB修饰]azazel
// [68] LayerA 修饰后返回数据: [LayerA修饰][LayerB修饰]azazel
// [68] Base 接收到最终数据: [LayerA修饰][LayerB修饰]azazel
// [68] ------------------------------------
// [68] 
// [68] --- 运行精确跳转: super[LayerA].action ---
// [68] LayerA 修饰后返回数据: [LayerA修饰]azazel
// [68] Base 接收到最终数据: [LayerA修饰]azazel
// [68] ------------------------------------
// [68] 
// [68] --- 运行精确跳转: super[Base].action ---
// [68] Base 接收到最终数据: azazel
// [68] ------------------------------------
// [68] 
// [68] ------------------------------------
// [68] 
// [68] === 测试完成 ===


