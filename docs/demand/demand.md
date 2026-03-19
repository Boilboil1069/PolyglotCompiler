这个文档是需求文档，请根据日期次数分割线进行实现。

要求:

1.在每次回答时都要有称呼MC；
2.代码中的注释使用英文；
3.不允许最小实现/占位等；
4.审查全项目，实现的代码要与全项目的风格符合；
5.生成的文档要中英双语两份文档;
6.不允许删库操作；
7.每次修改后都要检查是否需要修改文档，如有需要请修改相关的所有文档。

日期次数分割线示例如下：
```
（分割线头）2026-02-19-1

（内容）

（分割线尾）--end -done（完成标记）
```

2026-02-19-1

现在我要实现整个连接器最核心的部分，就是函数级链接，需要实现不同语言的函数级链接；具体示例如下：

```c++
function A(int a , float b){
    ....
    return c
}
```
```python 
function B (d,e):
    ...
    return f
```
假设这里的c++中的函数的输入需要python函数的输出，要实现这种链接的编译，当然不要只限于函数，其他也要实现。

当然，实现这种链接需要你设计一种新的语言，后缀名是.ploy，需要能实现这种实现，比如：
```ploy
LINK(TARGET_LANGUAGE, SOURSE_LANGUAGE, TARGET_FUNCTION, SOURSE_FUNTION)
```
要实现条件语句，循环语句等控制语句，具体实现细节需要写在docs\realization\中。

--end -done

2026-02-19-2

现在需要你扩张支持输入的参数，比如python中的列表，元组等，c++与如rust中的其他结构，对于一些复杂的，可以通过ploy函数实现映射。

最后，请帮我把已有的文档翻译成中文。

--end -done

2026-02-19-3

1.这次需要重新翻译以前的英文文档到中文，并且添加详细说明，比如ploy_language_spec.md这个文档中每条中添加解释。
2.添加更多的example。
3.请问这个实现后能否实现混合编译不同的语言。
4.能否支持一些语言的包导入呢？比如python的numpy等。
5.语法设计与文档有冲突，比如你的实现是：LINK cpp::graphics::draw_point AS FUNC(ptr) -> void而文档中是：LINK(target_language, source_language, target_function, source_function);
6.而且语句终止再示例中没有分号，在文档中需要分号。
7.请把语法与文档同一。

--end -done

2026-02-19-4

1.实现：
| 功能 | 语法概念 | 说明 |
|------|---------|------|
| 版本约束 | `IMPORT python PACKAGE numpy >= 1.20;` | 指定包的最低版本要求 |
| 选择性导入 | `IMPORT python PACKAGE numpy::(array, mean);` | 仅导入特定函数 |
| 包自动发现 | 自动检测 | 自动检测已安装的包并提供补全 |
| 虚拟环境支持 | 配置选项 | 指定使用特定的 Python 虚拟环境 |

2.这个文件中的mixed_compilation_analysis_zh.md的描述是否与整个项目的实现不同呢？


--end -done

2026-02-19-5

1.python添加多种包管理器支持，比如conda，uv等；
2.现在的混合编译方式是与mixed_compilation_analysis_zh.md文档中3.2 PolyglotCompiler 的混合编译方式描述的一样吗？
3.审查整个项目重写这个文档docs\POLYGLOT_COMPILER_COMPLETE_GUIDE.md

--end -done

2026-02-19-6

1.在docs\realization\package_management_zh.md中// 与版本约束和别名组合
IMPORT python PACKAGE numpy::(array, mean) >= 1.20 AS np;这会引起歧义，请修改。 

2.在mixed_compilation_analysis_zh.md中
──────────────┐  ┌──────────────┐  ┌──────────────┐
│  C++ 编译器   │  │ Python 解释器 │  │  Rust 编译器  │
│  (MSVC/GCC)  │  │  (CPython)   │  │   (rustc)    │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘
这些编译器与解释器使用了吗？使用的具体部分在哪？

--end -done

2026-02-20-1

在docs\realization\package_management_zh.md中的// 完整组合：选择性导入 + 版本约束 + 别名
// 含义：从 torch 包（要求版本 >= 2.0）中导入 tensor 和 no_grad，包别名为 pt
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0 AS pt;
不允许这种形式的出现，因为pt不知道指定的是torch::tensor还是torch::no_grad。

--end -done

2026-02-20-2

1.ploy语言需要不仅仅支持函数调用，还要支持某些语言的类的实例化等功能

--end -done

2026-02-20-3

0.添加类实例化的混合调用示例，比如cpp的类实例化使用的是python某一方法的返回值。

1.帮我根据项目写一个readme.md

2.帮我把docs\POLYGLOT_COMPILER_COMPLETE_GUIDE.md名字改为USER_GUIDE.md并且分出中英文两版

3.把你刚才实现的写入修改后的USER_GUIDE.md中，中英两版都要写入

4.实现这些：

| 限制 | 说明 |
|------|------|
| **静态类型未知** | `NEW` 和 `METHOD` 返回 `Any` 类型，无法在编译时进行完整的类型检查 |
| **无析构器** | 当前不支持自动调用析构器/`__del__`；需要手动调用 `METHOD(lang, obj, close)` |
| **无继承** | `.ploy` 不支持在目标语言中定义子类 |
| **无属性赋值** | 当前只能通过 `METHOD` 调用 setter，不能直接 `obj.attr = value` |

- **属性访问语法糖**：`GET(lang, obj, attr)` / `SET(lang, obj, attr, value)`
- **自动资源管理**：类似 Python 的 `with` 语句，自动调用 `__enter__`/`__exit__`
- **类型注解**：`LET model: python::nn::Module = NEW(python, torch::nn::Linear, 10, 5);`
- **接口映射**：通过 `MAP_TYPE` 将外部类映射到 `.ploy` 结构体

5.现有ploy代码内部有cpp与python代码，编译后的二进制文件是编译了cpp与python后混合而成的吗？

--end -done

2026-02-20-4

0.这些限制还存在吗？如果存在请清除这些限制，并更新文档（包含USER_GUIDE）：
| 限制 | 说明 |
|------|------|
| **静态类型未知** | `NEW` 和 `METHOD` 返回 `Any` 类型，无法在编译时进行完整的类型检查 |
| **无析构器** | 当前不支持自动调用析构器/`__del__`；需要手动调用 `METHOD(lang, obj, close)` |
| **无继承** | `.ploy` 不支持在目标语言中定义子类 |
| **无属性赋值** | 当前只能通过 `METHOD` 调用 setter，不能直接 `obj.attr = value` |

1.我现在需要实现一个细的报错信息，比如cpp有一个函数a(arg1,arg2)需要两个参数，但是传入的python函数b返回了一个参数arg1，需要实现检查出来这个错误，最好是能够在以后ui界面实现时，能在编译前就在ui中就能报错出来。

2.需要支持类型传入的错误检查。

3.最好加入很多完整的错误检查与报错信息与trackbacks。

4.这个compilation_model_zh.md文档的示例代码， CALL(cpp, image_processor::enhance, input);在后面并没有使用，而且不能一步命令编译吗？一定要三个文件一起编译后链接？我想要实现编译poly文件时自动编译python与cpp的功能，而且示例的cpp函数enhance(double* data, int size)参数时两个而这就变成了一个CALL(cpp, image_processor::enhance, input);。

--end -done

2026-02-20-5

0.这个compilation_model_zh.md文档的示例代码不应该是LET enhance_result = CALL(cpp, image_processor::enhance, input, size);LET result = CALL(python, ml_model::predict, enhance_result);，不应该是这样吗？
1.每一个示例文件与内容上的示例都要像这个compilation_model_zh.md文档的示例代码，有其他语言的代码，有poly代码，以便展示内容，而且samples文件中的代码要是能被编译成功可以运行的。
2.samples文件夹中按照ploy内容分类分文件夹，每个文件夹有ploy文件，与ploy中说的cpp，python与rust文件。
3，实现tests文件夹下的integration（集成测试）文件夹与benchmarks（性能基准）文件夹。

--end -done

2026-02-20-6 

1.我需要在samples中内置一个python环境，以便samples内部调用，同样的要有一个rust环境（环境都在env文件夹内），但是不要被git同步，要有一个bash文件以便创建环境，（windows是powershell文件）
2.当我运行D:\Others\PolyglotCompiler\build\polyc.exe .\basic_linking.ploy -0 basic_linking时，应该有进度的输出，但是现在什么都没有，并且运行速度很慢，内存占用很高，请帮我修改，是不是应该有中间的辅助文件的输出呢？如果有请输出在同文件夹下aux文件夹下，没有aux时，需要程序自动生成。

--end -done

2026-02-20-7

1.编译生成的二进制lib应该是有的，不应该全部编译在一个文件里。
2.帮我通过polyc编译samples里面的01-10sample，要成功编译，不要有报错。

--end -done

2026-02-20-8

1.运行tests\samples\setup_env.ps1时报错：

```bash
============================================
 PolyglotCompiler Sample Environment Setup
============================================

[OK] env/ directory already exists

--- Setting up Python environment ---
[..] Creating Python virtual environment...
[OK] Found Python: Python 3.10.15
[OK] Python venv created at: D:\\Others\\PolyglotCompiler\\tests\\samples\\env\\python
[OK] Found Python: Python 3.10.15
[OK] Python venv created at: D:\\Others\\PolyglotCompiler\\tests\\samples\\[OK] Found Python: Python 3.10.15
[OK] Python venv created at: D:\\Others\\PolyglotCompiler\\tests\\samples\\env\\python
[..] Installing sample dependencies...
pip.exe : ERROR: To modify pip, please run the following command:     
所在位置 D:\Others\PolyglotCompiler\tests\samples\setup_env.ps1:73 字 符: 13
+             & pip install --upgrade pip 2>&1 | Out-Null
+             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (ERROR: To modif...lowing command::String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
```

2.ploy等语言的编译目标不应该是二进制可执行文件吗？不能到asm就结束了，
3.需要将aux文件夹内的内容全部变为二进制文件，而不是明文在里面。

--end -done

2026-02-20-9

1.帮我添加java的支持，需要支持java8、java17，java21，java23的功能。
2.帮我添加.net的支持，需要支持.net6、7、8、9

--end -done

2026-02-20-10

1.补全：docs\api\内文档；
2.补全：docs\specs\内文档；
3.为什么sample创建rust环境时会有：Rust environment not set up.Install rustup from https://rustup.rs/ and re-run setup_env.ps1，这不能直接安装进去吗？
4.添加samples\setup_env中，添加java与.net环境的创建；
5.将局部的虚拟环境都连接到samples文件夹中各个samples；
6.整理samples文件夹，删除不必要的项，删除前询问我；
7.生成更多samples，并检查编译。

--end -done

2026-02-21-1

1.帮我再docs文件下建立一个tutorial文件夹，里面放入详细的ploy语言教程与详细的项目教程

--end -done

2026-02-21-2

修复这些：

1.polyc 对 python/rust 仍未接入真实 IR 降低，最终会回退生成假程序。driver.cpp (line 1428)、driver.cpp (line 1448)、driver.cpp (line 1489)

2.后端若没产出 section 会静默塞入 stub 机器码继续产物输出，掩盖真实失败。driver.cpp (line 1560)、driver.cpp (line 1571)

3.跨语言链接器在符号找不到时创建 placeholder 并返回成功，没有硬失败机制。polyglot_linker.cpp (line 121)、polyglot_linker.cpp (line 154)

4.marshalling 仍大量占位：NOP/placeholder call，且部分转换调用 relocation 根本没挂入 stub。polyglot_linker.cpp (line 370)、polyglot_linker.cpp (line 451)、polyglot_linker.cpp (line 458)、polyglot_linker.cpp (line 486)

5..ploy 降低层生成的运行时符号在 runtime 里缺实现（会导致真实链接/运行断裂）。lowering.cpp (line 1502)、lowering.cpp (line 1504)、lowering.cpp (line 1506)、lowering.cpp (line 1606)lowering.cpp (line 496)、lowering.cpp (line 532)（全仓未找到这些符号定义）

6.Java/.NET runtime bridge 还是 stub，核心调用返回 NULL 或空操作。java_rt.c (line 59)、dotnet_rt.c (line 63)

7.LTO 工作流仍非真实编译链：所谓 bitcode/object 目前是文件拷贝级实现。link_time_optimizer.cpp (line 118)、link_time_optimizer.cpp (line 1955)、link_time_optimizer.cpp (line 1983)

8.polyopt 目前不处理输入文件，只优化空 IRContext 后打印 optimized。optimizer.cpp (line 20)

9.polybench 大量 TODO，编译性能/E2E 基准并未落地。benchmark_suite.cpp (line 214)、benchmark_suite.cpp (line 297)

10..ploy 的跨语言静态类型/参数检查仍偏弱（大量 Any，LINK 签名默认未知参数个数）。sema.cpp (line 168)、sema.cpp (line 1339)、sema.cpp (line 1357)、lowering.cpp (line 737)

11.工程化还缺：E2E 测试被 #if 0 整体禁用，且 .github 为空（无 CI workflow）。e2e_compilation_test.cpp (line 20)

12..ploy 关键字参数（命名实参）尚未实现，AST 和 parser 仅支持位置参数。ploy_ast.h (line 96)、parser.cpp (line 1463)

--end -done

2026-02-21-3

修复这些：

1.polyrt 没有 FFI 管理能力（文档写了有，实际没有命令）
README.md (line 232) 写的是 GC/FFI/Thread，但 polyrt 只支持 status/gc/thread/bench/info，没有 ffi 子命令：polyrt.cpp (line 129)、polyrt.cpp (line 759)。

2.polyrt 统计基本是“本地计数器”，没接 runtime 真指标
统计来自 g_stats：polyrt.cpp (line 119)；GC/线程展示大量硬编码或手工累加：polyrt.cpp (line 200)、polyrt.cpp (line 329)。而 GC 接口只有分配/回收/root 注册，没有查询统计的 API：gc_api.h (line 10)。

3.链接器依赖的两个 runtime 符号仍缺实现
polyld 会发射 __ploy_rt_convert_cppvec_to_list、__ploy_rt_convert_list_generic：polyglot_linker.cpp (line 500)、polyglot_linker.cpp (line 502)；runtime 里没有对应导出。

4.Python 容器转换仍是占位实现
直接返回原指针或空壳容器：container_marshal.cpp (line 213)、container_marshal.cpp (line 224)。

5.线程 Profiling 接口只有声明，没有实现
ThreadProfiler 只在头文件声明：threading.h (line 328)，threading.cpp 无对应定义。

6.Java/.NET bridge 仍是“可退化”模式，失败时大量返回空
找不到运行时直接降级返回：java_rt.c (line 42)、dotnet_rt.c (line 84)；.NET host 初始化参数也非常最小（TRUSTED_PLATFORM_ASSEMBLIES 为空）：dotnet_rt.c (line 101)。

7.polyrt 本身还有可用性问题
源码里有明显语法错误：polyrt.cpp (line 724)；同时有编码乱码输出：polyrt.cpp (line 57)、polyrt.cpp (line 219)。

8.工程化上没有 polyrt 专项测试入口
CTest 只挂了 unit/integration/benchmark：CMakeLists.txt (line 364)、CMakeLists.txt (line 365)、CMakeLists.txt (line 366)。

--end -done

2026-02-21-4

修复这些：

1.LTO 还不是“真实编译链”
link_time_optimizer.cpp (line 118)、link_time_optimizer.cpp (line 1976)、link_time_optimizer.cpp (line 1981) 仍在用 placeholder 指令/函数；link_time_optimizer.cpp (line 1680)、link_time_optimizer.cpp (line 1684) 只是写文本摘要，不是目标码产物。

2.高级优化 Pass 大量空壳
advanced_optimizations.cpp (line 1476) 到 advanced_optimizations.cpp (line 1539) 这批函数基本是 (void)func;（SoftwarePipelining/LoopTiling/BranchPrediction 等）。

3.Devirtualization 仍是弱实现
devirtualization.cpp (line 106) 把 InferObjectType 传 nullptr，类型传播路径基本失效；devirtualization.cpp (line 162) 注释明确只做简化检查。

4.PRE/GVN 还有占位逻辑
gvn.cpp (line 371)、gvn.cpp (line 378) 的补偿代码插入仍是 placeholder 类型（固定 I32）。

5.模板实例化 AST 替换未落地
template_instantiator.cpp (line 445)、template_instantiator.cpp (line 447) 直接返回 Success(nullptr)，AST substitution 还没真正实现。

6.分析层公共接口还是壳
alias.h (line 5)、dominance.h (line 5) 只有空 struct，没形成可复用分析 API。

7.中端优化流程未真正接入 polyc
driver.cpp (line 179) 有 opt_level，但主流程只看到 SSA+Verify（driver.cpp (line 1514)、driver.cpp (line 1517)），没有把 middle 的优化 pipeline 真正跑起来。

8.middle 的测试仍有大量“占位通过”
optimization_passes_test.cpp (line 23)（TODO）及大量 REQUIRE(true)，很多优化并没有被行为级断言锁定。

--end -done

2026-02-21-5

修复这些：

1..ploy 跨语言 LINK 仍是占位级 lowering：wrapper 固定只放一个 arg0:i64，返回值默认 i64，不是按真实签名生成。lowering.cpp (line 1241) lowering.cpp (line 1248) lowering.cpp (line 1250)

2..ploy 静态类型检查仍偏弱：大量退化为 Any，且 LINK 参数个数依赖 MAP_TYPE 条目推断。sema.cpp (line 152) sema.cpp (line 159) sema.cpp (line 783) sema.cpp (line 825)

3.命名实参语法有了，但语义上名字基本被忽略（只按 value 处理）。sema.cpp (line 688) lowering.cpp (line 533)

4.Java/.NET 前端 lowering 发出的运行时符号与 runtime 实现不对齐：前端调用 __ploy_java_*/__ploy_dotnet_*，runtime 侧主要导出 polyglot_java_*/polyglot_dotnet_*，且多项 __ploy_* helper 未实现。lowering.cpp (line 271) lowering.cpp (line 496) lowering.cpp (line 278) lowering.cpp (line 532) java_rt.c (line 180) dotnet_rt.c (line 133)

5.C++ lowering 仍有明显未覆盖语义。lowering.cpp (line 821) lowering.cpp (line 1478) lowering.cpp (line 1505) lowering.cpp (line 1519)

6.Python lowering 仍有未完整实现路径（如 starred args）。lowering.cpp (line 349) lowering.cpp (line 926) lowering.cpp (line 2004)

7.Rust lowering 仍有“skip/not fully supported”分支（宏调用、tuple 解构等）。lowering.cpp (line 774) lowering.cpp (line 827) lowering.cpp (line 1209)

8.Java/.NET parser 对外暴露的部分接口仍是空实现返回 nullptr。parser.cpp (line 1279) parser.cpp (line 1479)

--end -done

2026-02-21-6

修复这些：

1.跨语言链接主链路没接上
polyc 只把 descriptor 写到 aux，没有把它喂给 PolyglotLinker（driver.cpp (line 1408)）。polyld 主流程里也没调用 ResolveLinks()（linker.cpp 全文件无调用点）。

2..ploy 生成的桥接 stub 仍是占位签名
GenerateLinkStub 里参数还是固定 arg0: i64（lowering.cpp (line 1248)），不是按真实函数签名生成。

3.linker 依赖的 runtime 符号还缺
__ploy_rt_convert_cppvec_to_list、__ploy_rt_convert_list_generic 在 linker 会发射（polyglot_linker.cpp (line 500), polyglot_linker.cpp (line 502)），runtime 里没有实现。

4.Python 容器互转还是 placeholder
list->pylist / dict->pydict 直接返回原指针（container_marshal.cpp (line 213), container_marshal.cpp (line 224)）。

5.wasm backend 还没落地
backends/wasm 目录是空骨架，且 CMake 没有 backend_wasm 目标（CMakeLists.txt）。

6.自研 linker 功能不完整
COFF/PE 加载未实现（linker.cpp (line 1118)）；可重定位/静态库输出未实现（linker.cpp (line 2302), linker.cpp (line 2306)）；Mach-O 可执行/动态库未实现（linker.cpp (line 2454), linker.cpp (line 2459)）。

7.LTO / polyopt 仍偏占位
LTO 里仍有 placeholder instruction / placeholder 函数路径（link_time_optimizer.cpp (line 118), link_time_optimizer.cpp (line 1976)）；polyopt 还是空 context 走 pass（optimizer.cpp (line 88)）。

8.backend 级跨语言 E2E 还不够
现有 integration 多数停在"词法-语义-lowering-IR"，没有覆盖真实"跨语言链接+产物运行"链路（compile_pipeline_test.cpp (line 39)）。

--end -done

2026-02-21-7 -done

修复这些：

1.common 调试抽象里有“未落地且当前不可用”的接口
debug_info_builder.h (line 315) 使用了未定义的 dwarf::DwarfContext；而且该头里又定义了一套 SourceLocation（debug_info_builder.h (line 26)），和 dwarf5.h (line 129) 重复，后续一旦接入会直接冲突。

2.后端 common 的对象文件能力不完整
object_file.cpp (line 267) 的 MachOBuilder::Build() 只写了最小头部就返回。
object_file.cpp (line 100) ELF e_machine 固定写 EM_X86_64，没有按目标切换。
object_file.cpp (line 21) 定义了 SHT_RELA，但构建流程里没有真正落地 section relocation。

3.Debug 发射器实现仍是简化版，不是“完整链路”
debug_emitter.cpp (line 894)（CFA 初始化简化）
debug_emitter.cpp (line 996)（GUID 生成简化）
debug_emitter.cpp (line 1061)（TPI type record 为空）
debug_emitter.cpp (line 1214)（PDB stream block 布局简化）

4.DWARF 代码路径重复且分裂
仓库里有三套相关实现：dwarf5.cpp、dwarf_builder.cpp、debug_emitter.cpp 内部 builder。
其中 dwarf_builder.cpp 目前未编进库（CMakeLists.txt (line 10)）。

5.“公共 IR” 的 parser/printer 还不对称
printer.cpp (line 41) 会打印 fadd/fsub/fmul/fdiv/frem，但 parser.cpp (line 127) 的 ParseBinOp 没解析这些。
printer.cpp (line 221) 会输出 global/const，但 parser.cpp (line 524) 的 ParseModule 只拼函数文本，忽略 module 级全局。

6.frontends/common 缺少专门测试覆盖
preprocessor.cpp 逻辑很重，但 tests 里没有专门 preprocessor/token_pool 用例；这块回归风险较高。

7.debug 相关测试在 Windows 路径不兼容
runtime_tests.cpp (line 188)、runtime_tests.cpp (line 220)、runtime_tests.cpp (line 250) 硬编码 /tmp/...，导致 [debug] 在当前 Windows 环境直接失败。

--end -done

2026-02-22-1

1.根据整个项目更新docs中所有文档与README.md

--end -done

2026-02-22-2

1.USER_GUIDE_zh.md文档的4.4 混合编译方式，需要加入新支持的语言。
2.按照文档内容重写docs\specs\Namespace_Architecture_And_Usage_Analysis.md这个文档（不需要解决方案建议），并且重写后重新命名，双语文档，删除旧文档。

--end -done

2026-02-22-3

1.链接器对未解析符号改为强失败，禁止占位符继续链接；修改 tools/polyld/src/polyglot_linker.cpp。
2.polyld 主流程补齐输入实体与描述符接线，避免空状态调用 ResolveLinks()；修改 tools/polyld/src/linker.cpp。
3.polyc 增加 wasm 架构选项并接入后端分发；修改 tools/polyc/src/driver.cpp。
4.polyasm 增加 wasm 架构选项；修改 tools/polyasm/src/assembler.cpp。
5.WASM 后端补齐真实调用目标索引生成，移除硬编码占位；修改 backends/wasm/src/wasm_target.cpp。
6.WASM 后端实现 alloca 的合法降级与栈模型，移除 nop 占位；修改 backends/wasm/src/wasm_target.cpp。
7.WASM 后端对未支持 IR 指令改为显式诊断错误，避免静默 nop；修改 backends/wasm/src/wasm_target.cpp。
8..ploy 语义分析增加 strict 模式，减少或禁止 Any 回退；修改 frontends/ploy/src/sema/sema.cpp。
9..ploy lowering 去除默认 I64 回退，按签名与推断生成类型；修改 frontends/ploy/src/lowering/lowering.cpp。
10.完善调试信息生成，补齐 .eh_frame FDE 与 PDB 类型流；修改 backends/common/src/debug_emitter.cpp。
11.完善 Mach-O 节重定位信息写入（reloff/nreloc）；修改 backends/common/src/object_file.cpp。
12.逐步清理 Rust/Python/C++/.NET 前端中的 unsupported 或简化路径；修改 frontends/rust/src/lowering/lowering.cpp、frontends/python/src/lowering/lowering.cpp、frontends/cpp/src/lowering/lowering.cpp、frontends/dotnet/src/parser/parser.cpp。
13.清理占位测试，将 REQUIRE(true) 替换为行为断言；重点修改 tests/unit/frontends/rust/advanced_features_test.cpp。
14.增加编译-链接-运行一体化 e2e 测试，覆盖 x86_64 与 arm64，WASM 先补 smoke；扩展 tests/integration/compile_tests/compile_pipeline_test.cpp。
15.同步更新能力边界与使用说明文档；修改 README.md、docs/USER_GUIDE.md、docs/USER_GUIDE_zh.md。

--end -done

2026-02-22-4

1. 新增 `PloySemaOptions` 配置结构，支持 `enable_package_discovery` 开关；修改 `frontends/ploy/include/ploy_sema.h`。
2. `PloySema` 构造函数接收 options，并保持默认行为向后兼容；修改 `frontends/ploy/include/ploy_sema.h`。
3. 将 `discovery_completed_` 与 `discovered_packages_` 从实例级迁移为会话级可复用缓存服务；新增 `frontends/ploy/include/package_discovery_cache.h`、`frontends/ploy/src/sema/package_discovery_cache.cpp`。
4. 为 discovery 缓存增加线程安全与统一 key（`language + manager + env_path`）；修改 `frontends/ploy/src/sema/package_discovery_cache.cpp`。
5. 抽离外部命令执行层（如 `ICommandRunner`），避免语义分析直接 `_popen`；新增 `frontends/ploy/include/command_runner.h`、`frontends/ploy/src/sema/command_runner.cpp`。
6. `DiscoverPackages` 流程改为先查缓存，未命中才执行外部命令并回填缓存；修改 `frontends/ploy/src/sema/sema.cpp`。
7. benchmark 场景显式关闭 package discovery，确保只测编译链路本体；修改 `tests/benchmarks/micro/micro_bench.cpp`、`tests/benchmarks/macro/macro_bench.cpp`。
8. 修正 `Lowering` micro benchmark 的计时边界，去除计时外重开销对结果的干扰；修改 `tests/benchmarks/micro/micro_bench.cpp`。
9. 拆分 benchmark 运行档位（fast/full），通过环境变量控制 `kWarmup` 与 `kRuns`；修改 `tests/benchmarks/micro/micro_bench.cpp`、`tests/benchmarks/macro/macro_bench.cpp`。
10. CTest 增加 benchmark 标签或拆分 test target（如 `benchmark_fast`、`benchmark_full`）；修改 `CMakeLists.txt`。
11. 增加单元测试覆盖：`discovery disabled` 不触发外部命令、同配置重复 `Analyze` 仅发现一次、不同 key 不串缓存；新增 `tests/unit/frontends/ploy/sema_discovery_test.cpp`。
12. 更新文档说明 discovery 开关、缓存策略、benchmark 推荐构建类型（Release/RelWithDebInfo）；修改 `docs/USER_GUIDE.md`、`docs/USER_GUIDE_zh.md`。

--end -done

2026-02-22-5

1. 解除 `common` 与 `middle` 的循环依赖：
   - 方案将 `common/include/ir/*` 下接口迁入 `middle/include/ir/*`。
2. 固化层级约束：在 CI 增加 include-lint 规则，禁止新出现的逆向依赖。
3. 统一调试信息建模：`common::debug` 与 `backends::DebugInfoBuilder` 功能重叠，建议定义单一数据模型与转换边界。
4. 补齐“高级优化/PGO/LTO”与默认管线的集成文档与开关矩阵，避免能力存在但入口分散。
5. 将运行时 C ABI 作为独立接口层文档化（建议新增 `runtime_abi.md`），并明确与 C++ API 的职责边界。
6. 对工具层命名空间做一致化：当前 `polyc` 主要匿名命名空间，`polyasm/polyopt/polyrt` 使用 `polyglot::tools`，风格不统一。

--end -done

2026-02-22-6

1.修复编译错误。
2.编译其他平台上失败，编译要在LINUX与MACOS平台通过

--end -done

2026-02-22-7

修复在wsl上编译的错误：

1.先修复阻塞编译错误（唯一 error）
class_metadata.h (line 87) 的 std::find(...) 报错，核心修改是补齐算法头（通常是 #include <algorithm>，位置在 class_metadata.h (line 1) 附近）。

2.统一修复 Type 的缺省初始化（最高频 warning）
types.h 大量 -Wmissing-field-initializers，集中在 Type::language、Type::type_args、Type::lifetime（日志中重复最多）。

3.修复 IRType::subtypes 未初始化
types.h 多处构造触发 IRType::subtypes 缺失初始化告警。

4.修复 Symbol::access 与作用域状态结构体的初始化缺失
主要在 sema.cpp、sema.cpp、sema.cpp、sema.cpp。

5.Rust 词法器字符常量问题
lexer.cpp (line 240) 的字符比较触发 -Wmultichar 和 -Wtype-limits，同文件还有 byte_prefix 未使用告警（lexer.cpp (line 178)）。

6.清理低优先级未使用变量/函数
分布在 lowering.cpp、object_file.cpp、cpp_constexpr.cpp、debug_emitter.cpp 等文件。

--end -done

2026-03-01-1

1.我要实现一个编译器的ui界面，要支持语法高亮、错误提示等编译器功能。

--end -done

2026-03-11-1

1.Qt的目录在这里：D:\Qt，请不要在项目中引用的是anaconda的qt内容；
2.构建的时候要默认生成可执行文件。

--end -done

2026-03-11-2

1.双击编译后的polyui.exe会提示找不Qt6widgestsd.dll，Qt6Guid.dll与Qt6Cored.dll这三个文件

--end -done

2026-03-11-3

1.将tools/ui中的代码按照不同平台分开，在cmake时不同平台执行不同的编译。

--end -done

2026-03-11-4

1.在ide的gui中要支持terminal。
2.删除ui文件夹中旧的代码：ployui_windows与src与include

--end -done

2026-03-12-1

1.在macos上没有构建ui，我没有安装qt，你可以帮我下载qt-6.10.2源代码，进行构建吗？最后要能构建ui。

--end -done

2026-03-12-2

1.gui中需要完整的设置页面，并支持修改默认设置。
2.支持git。
3.支持cmake等构建工具。
4.支持断点等debug选项。

--end -done

2026-03-15-1

实现一下功能：
1.Compile & Run 和 Stop 仍是占位逻辑。见 mainwindow.cpp、mainwindow.cpp；
2.调试面板“调用栈/变量/watch”解析仍不完整，核心依赖输出解析未落地；OnRemoveWatch 有实现但 UI 未接线。见 debug_panel.cpp、debug_panel.cpp、debug_panel.cpp、debug_panel.cpp；
3.题系统不完整。Settings 提供多主题，但大量面板仍硬编码 setStyleSheet，导致主题难统一。见 settings_dialog.cpp、mainwindow.cpp、git_panel.cpp、build_panel.cpp、debug_panel.cpp；
4.自定义快捷键仍未实现（当前仅展示默认键位）。见 settings_dialog.cpp；
5.Settings 里的部分"环境/构建/调试路径"尚未完全联动到 BuildPanel/DebugPanel 运行参数链路。

--end -done

2026-03-15-2

我需要一个不同平台的release版本的打包脚本，最好能打包成windows下的安装程序和免安装版本，linux与macos不需要打包成安装包，仅要免安装版本；将全项目的版本代码改为1.0.0，已有的比如5.1版本号改为0.5.1版本。

--end -done

2026-03-15-3

实现插件功能，设计插件规范与接口。

--end -done

2026-03-15-4

修复：
类别	位置（全量）	问题表现	修复可能逻辑
关键链路回退	driver.cpp:1439, driver.cpp:1442, driver.cpp:1445, driver.cpp:1452	polyc 里做了 ResolveLinks()，但仅注释说明可取 GetStubs()，未见把 stub 真正并入输出 section/symbol/reloc。	在 polyc 中把 poly_linker.GetStubs() 映射成后端可消费的节和重定位，或统一落盘 descriptor 并由 polyld 消费。
关键链路回退	linker.cpp:3468, linker.cpp:3469, polyglot_linker.cpp:50, polyglot_linker.cpp:54, polyglot_linker.cpp:58	polyld 主流程调用 ResolveLinks() 前未看到注入 descriptors/link entries/symbols，跨语义链接容易空跑。	给 polyld 增加输入协议（--ploy-desc/--link-entry/从 aux 读取），在主流程显式 AddCallDescriptor/AddLinkEntry/AddCrossLangSymbol。
关键链路回退	polyglot_linker.cpp:75, polyglot_linker.cpp:88, polyglot_linker.cpp:106	对“无 LINK 覆盖”的 CALL 走“minimal glue stub”合成路径，属于回退策略。	默认改为严格模式：无 LINK/签名时报错；保留 --allow-adhoc-link 作为显式开关。
关键链路回退	driver.cpp:1662, driver.cpp:1669, driver.cpp:1682	--force 下后端无 section 仍会生成最小 stub 继续产物。	CI/Release 禁用该回退；仅 Debug 允许；并在产物 metadata 写入“degraded build”。
关键链路回退	wasm_target.cpp:360	未解析 callee 仍“emitting index 0”。	改为硬错误；或生成 import 占位并输出 unresolved 符号清单给链接阶段。
关键链路回退	wasm_target.cpp:476, wasm_target.cpp:819	br/br_if 深度固定 0（待 patch）。	先构建结构化 CFG 到 block-depth 映射，再发射正确深度。
关键链路回退	wasm_target.cpp:491	unsupported IR 指令直接降级为 unreachable。	引入“诊断失败即停止发射”策略；仅在显式容错模式允许 trap。
关键链路回退	wasm_target.cpp:321	frem -> f64.div 语义不等价。	使用 runtime helper 或合法 lowering（x - trunc(x/y)*y）实现。
类型系统回退（签名约束变松）	sema.cpp:200, sema.cpp:1470, sema.cpp:1488	LINK 注册签名时 param_count_known=false，导致参数个数/类型校验直接跳过。	扩展语法显式声明 LINK 函数签名；或把可推断参数数目设为 known，并对未知参数标 Unknown 而非跳过校验。
类型系统回退（Any）	sema.cpp:184（其余命中行：186, 355, 441, 512, 595, 669, 680, 689, 691, 728, 737, 782, 853, 899, 948, 995, 1020, 1053, 1081, 1134, 1137, 1166, 1258, 1289, 1307, 1331, 1336, 1356, 1357, 1372, 2004, 2027）	.ploy sema 多处默认 Any（表达式、对象属性、容器推断、EXTEND/self 等）。	采用三态类型：Concrete/Unknown/Any；Unknown 在边界处要求显式注解；默认 strict 模式。
类型系统回退（I64）	lowering.cpp:782（其余命中行：827, 828, 839, 840, 845, 924, 925, 936, 937, 942, 970, 971, 982, 983, 988）	跨语言 CALL/METHOD/GET 默认返回 I64。	用 sema 完整签名驱动 lowering；无签名时报错，不再静默 I64。
类型系统回退（stub 签名）	lowering.cpp:1340, lowering.cpp:1362, lowering.cpp:1371, lowering.cpp:1383, lowering.cpp:1385	GenerateLinkStub 在未知签名时回退 arg0:i64 + ret:i64。	强制 LINK 提供完整参数与返回类型；对不完整 LINK 拒绝生成 stub。
类型系统回退（类型映射兜底）	lowering.cpp:1534, lowering.cpp:1547, lowering.cpp:1583, lowering.cpp:1620	CoreTypeToIR/TypeNodeToIR 未识别类型回退 I64。	未识别类型应产生日志+错误码并中断编译；避免 silent cast。
类型系统回退（IR 验证层）	verifier.cpp:54, verifier.cpp:92, verifier.cpp:310, verifier.cpp:683	verifier 对 placeholder/I64 有特例容忍，降低了错误暴露强度。	引入 -Werror-placeholder-ir；在非开发模式拒绝 placeholder 通过验证。
防崩溃式断言	devirtualization_test.cpp:186, devirtualization_test.cpp:251	REQUIRE(true) 仅验证“不崩溃”。	改为校验 devirt 前后 callee、调用图、性能计数变化。
防崩溃式断言	preprocessor_tests.cpp:157	同上。	增加 token 序列、宏替换结果、错误定位断言。
防崩溃式断言	loop_optimization_test.cpp:678	同上。	校验循环变换前后 IR 等价性与指令计数变化。
防崩溃式断言	threading_services_test.cpp:45（其余：113, 138, 208, 218, 256, 336, 520）	同上。	增加任务完成数、竞争条件、超时与资源回收断言。
防崩溃式断言	lto_test.cpp:493（其余：590, 804, 832）	同上。	校验跨模块 inline/符号消解/导出表和产物一致性。
防崩溃式断言	polyrt_test.cpp:123	同上。	校验 CLI 子命令输出、状态字段和错误码。
防崩溃式断言	gc_algorithms_test.cpp:140（其余：198, 216, 268, 276）	同上。	校验存活对象数、回收量、暂停时间边界。
防崩溃式断言	borrow_checker_test.cpp:236	同上。	校验具体借用冲突诊断与 span。
文档与实现落差	README.md:20, README.md:232, mixed_compilation_analysis.md:9	README 强调统一 IR/统一原生二进制；mixed_compilation_analysis 又写“不是 unified compilation（object code level）”，叙述互相冲突。	统一术语：定义“统一 IR 编译”与“函数级跨语链接”边界，文档只保留一种主叙事。
文档与实现落差	compilation_model.md:82, compilation_model.md:99, 对照 driver.cpp:1452, linker.cpp:3468	文档称 descriptor→PolyglotLinker→统一产物闭环；代码主流程仍缺 descriptor/link-entry 注入闭环。	先补链路再写文档；若短期不补，文档改为“partial / WIP”。
文档与实现落差	compilation_model.md:15, compilation_model.md:108, 对照 driver.cpp:1888, driver.cpp:1910	文档“完全不调用外部工具”表述过强；实际 link 阶段会 std::system 调用 polyld/link/lld-link/clang。	文档改为“前端不依赖外部编译器，链接阶段可调用系统链接器（可选）”。
文档与实现落差	mixed_compilation_analysis.md:93（其余：94~101）	“What Works Today” 全绿 Implemented，与实际回退路径（Any/I64/force/wasm 降级）不一致。	增加能力矩阵：Implemented / Partial / Experimental / Planned 四级。


--end -done

2026-03-17-1

先收紧“可编译”和“可正确交付”的边界。CLI 里 .ploy 语义分析仍走默认配置 ploy_sema.h (line 27) + driver.cpp (line 1362)，默认是 package discovery = on、strict_mode = false；IR verifier 也对占位型 I64/Invalid 比较宽松 verifier.cpp (line 91)，而 --force 还能继续生成 degraded stub driver.cpp (line 1690)。建议尽快明确两套模式：dev/permissive 和 release/strict，默认构建和 CI 都走 strict。

--end -done

2026-03-17-2

把"语言分发逻辑"抽成统一注册中心。现在 CLI 和 UI 都是按语言写长 if/else 分发 driver.cpp (line 1487) 、compiler_service.cpp (line 138) 、compiler_service.cpp (line 215)。建议引入统一的 FrontendRegistry / ILanguageFrontend，把 tokenize/parse/analyze/lower 收敛到一套接口，CLI、UI、测试、插件都共用。

--end -done

2026-03-17-3

把包发现和外部环境探测从语义分析主路径里移出去。现在 .ploy sema 默认会执行 pip、mvn、gradle、dotnet 命令 sema.cpp (line 1703) 、sema.cpp (line 2081) 、sema.cpp (line 2133) 、sema.cpp (line 2228)，底层还是直接 popen command_runner.cpp (line 12)。这会拖慢编译、引入环境不确定性，也让 UI/CLI 行为分叉，因为 UI 明确关掉了 discovery 并开了 strict compiler_service.cpp (line 229)。建议改成显式的 package-index 阶段，单独缓存，带超时和参数化进程调用。

--end -done

2026-03-17-4

重构构建系统。顶层 CMakeLists.txt (line 1) 已经很重，而且三个测试目标都直接把所有前端、后端、runtime 全链上去 CMakeLists.txt (line 608)。建议拆成按目录 add_subdirectory() 的模块化 CMake，并把测试按模块拆分，降低全量重编和链接成本。

--end -done

2026-03-17-5

提升测试质量，不要只看"能跑通"。仓库里仍有明显的空断言或弱断言，比如 lto_test.cpp (line 810)、gc_algorithms_test.cpp (line 140)、java_test.cpp (line 271)、dotnet_test.cpp (line 288)。建议把重点转向"编译产物行为级断言"：sample 编译、链接、运行、校验输出，以及失败路径测试。

--end -done  

2026-03-17-6

插件系统和 UI 需要从"有接口"变成"可扩展"。插件宿主回调里 emit_diagnostic/open_file/register_file_type 目前还是空实现 plugin_manager.cpp (line 117) 、plugin_manager.cpp (line 141) 、plugin_manager.cpp (line 148)，但又已经挂到了 host services plugin_manager.cpp (line 676)。UI 主窗口也过于"God Object"，布局、样式、业务逻辑都在一个大文件里 mainwindow.cpp (line 1)。这两块都适合做边界重构。

--end -done

2026-03-17-7

文档需要单源化和自动校验。现在 docs/ 下有 37 个 Markdown，而且中英双份，漂移风险很高；已经出现 README 里 Qt 安装脚本路径写成 scripts/setup_qt.*，但仓库实际在 tools/ui/ 的问题 README.md (line 97) 、README.md (line 390)。建议加 docs lint，并把高频文档改成模板生成或单源维护。

--end -done

2026-03-17-8

CI 还停留在”能编译+跑 ctest”。当前 workflow 基本只有 configure/build/test ci.yml (line 24) 、ci.yml (line 55) 、ci.yml (line 75)，缺少 ASan/UBSan、clang-tidy、格式检查、文档链接检查、覆盖率和 benchmark smoke。这个阶段最该补的是质量闸门，而不是再加平台矩阵。

--end -done

2026-03-19-1

帮我做一下ci检查，修复检查出现的错误。

--end -done

2026-03-19-2

补全真实的跨语言链接闭环。polyc 当前只把 CallDescriptor 和 LinkEntry 喂给 linker，没有把真实跨语言符号表接进去，driver.cpp (line 1545)；而 linker 的设计本身要求显式注册 CrossLangSymbol，polyglot_linker.h (line 61)。现有集成测试也是手工 AddCrossLangSymbol(...) 才能通过，compile_pipeline_test.cpp (line 776)。这说明跨语言能力在测试层是”手工闭环”，在主流程里还没有完全闭环。

--end -done

2026-03-19-3

收紧”降级成功”的路径，避免假成功掩盖真实失败。当前 polyc 在某些情况下会继续注入 stub 或最小 main，driver.cpp (line 1633) driver.cpp (line 1765)；.ploy lowering 里也还有大量 placeholder i64、单个 opaque 参数兜底、直接赋值兜底，lowering.cpp (line 821) lowering.cpp (line 1427) lowering.cpp (line 1513)。这类逻辑适合保留在显式开发模式，不适合继续作为默认容错路线。

--end -done

2026-03-19-4

把编译管线从“大型 driver”重构成真正的阶段化 pipeline。现在 driver.cpp 已经 2000+ 行，SSA、verify、O1/O2/O3 顺序全写死在 driver 里，driver.cpp (line 1647) driver.cpp (line 1663)。与此同时仓库里其实有一个 PassManager 抽象，但它只定义在 .cpp 内部、没有真正接入主链，pass_manager.cpp (line 13)。这会让后续所有优化、诊断、缓存都继续堆在 driver 上。

--end

2026-03-19-5

拆掉过度链接和测试耦合。测试配置把所有前端、所有后端、runtime、linker 全部链接进每个测试目标，tests/CMakeLists.txt (line 10) tests/CMakeLists.txt (line 161)。ctest -N 也只有 5 个顶层测试入口，意味着一处动态库加载问题就能让整套 800+ case 全部不可用。我本地执行 ctest --output-on-failure -R unit_tests 已经复现了这个问题，dyld 找不到 libfrontend_python.dylib；而当前 macOS RPATH 的 build-tree fallback 也确实不对，CMakeLists.txt (line 32)。建议改成按模块拆分测试二进制，并把测试默认做成静态或正确的 build-rpath。

--end

2026-03-19-6

修正模块边界。polyglot_common 现在直接把 backends/common 的实现源码编进来了，common/CMakeLists.txt (line 6)，这已经是明显的层级反转。类似地，polyld 的 CLI 逻辑也出现了漂移，真正的可执行入口在 main.cpp (line 103)，而更完整的命令行支持却散落在 linker.cpp (line 3440)。这类问题现在不处理，后面会越来越难拆。

--end

2026-03-19-7

把类型/ABI 检查前移到 sema 和链接阶段，而不是继续依赖“先降到 IR 再看”。现在 NEW/METHOD/GET/SET 仍大量依赖 opaque pointer 和 direct marshal，lowering.cpp (line 877) lowering.cpp (line 924) lowering.cpp (line 985)。WITH 注释写的是“即使异常也会调用 __exit__”，但当前 lowering 只是顺序插入 enter/body/exit，并没有异常控制流，lowering.cpp (line 1089)。这部分最值得补的是统一签名表、参数个数检查、返回值检查和 ABI schema。

--end

2026-03-19-8

运行时性能和内存策略。容器运行时目前还是 calloc/realloc/malloc 风格实现，dict 没有扩容/rehash 策略，container_marshal.cpp (line 47) container_marshal.cpp (line 134)；类扩展注册还是固定大小全局数组，也没有线程保护，object_lifecycle.cpp (line 81) object_lifecycle.cpp (line 93)。这部分会影响内存占用、并发安全和长期稳定性，但前提还是前面的主链先收敛。

--end

2026-03-19-9

CI 已经覆盖 docs、format、tidy、sanitizer、coverage、benchmark 和多平台构建，ci.yml (line 18)；这点是项目的优点。不过 clang-tidy 现在只扫前 50 个 .cpp，ci.yml (line 87)；.clang-format 仍写的是 c++17，而项目实际是 C++20，.clang-format (line 3) CMakeLists.txt (line 4)；tests/CMakeLists.txt 注释说避免 GLOB_RECURSE，但 integration/benchmark 还是用了 glob，tests/CMakeLists.txt (line 4) tests/CMakeLists.txt (line 168)。

--end