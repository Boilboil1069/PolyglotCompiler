这个文档是需求文档，请根据日期次数分割线进行实现。

要求:

1.在每次回答时都要有称呼MC；
2.代码中的注释使用英文；
3.不允许最小实现/占位等；
4.审查全项目，实现的代码要与全项目的风格符合；
5.生成的文档要中英双语两份文档;
6.不允许删库操作；
7.每次修改后都要检查是否需要修改文档，如有需要请修改相关的所有文档；
8.每次完成需求后要增加完成标记；
9.根据修改判断版本号的变化，对版本的修改在项目最原始目录的cmakelist中实现；
10.所有生成的注释与文档中不要出现与该文档相关的内容；

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

把编译管线从”大型 driver”重构成真正的阶段化 pipeline。现在 driver.cpp 已经 2000+ 行，SSA、verify、O1/O2/O3 顺序全写死在 driver 里，driver.cpp (line 1647) driver.cpp (line 1663)。与此同时仓库里其实有一个 PassManager 抽象，但它只定义在 .cpp 内部、没有真正接入主链，pass_manager.cpp (line 13)。这会让后续所有优化、诊断、缓存都继续堆在 driver 上。

--end -done

2026-03-19-5

拆掉过度链接和测试耦合。测试配置把所有前端、所有后端、runtime、linker 全部链接进每个测试目标，tests/CMakeLists.txt (line 10) tests/CMakeLists.txt (line 161)。ctest -N 也只有 5 个顶层测试入口，意味着一处动态库加载问题就能让整套 800+ case 全部不可用。我本地执行 ctest --output-on-failure -R unit_tests 已经复现了这个问题，dyld 找不到 libfrontend_python.dylib；而当前 macOS RPATH 的 build-tree fallback 也确实不对，CMakeLists.txt (line 32)。建议改成按模块拆分测试二进制，并把测试默认做成静态或正确的 build-rpath。

--end -done

2026-03-19-6

修正模块边界。polyglot_common 现在直接把 backends/common 的实现源码编进来了，common/CMakeLists.txt (line 6)，这已经是明显的层级反转。类似地，polyld 的 CLI 逻辑也出现了漂移，真正的可执行入口在 main.cpp (line 103)，而更完整的命令行支持却散落在 linker.cpp (line 3440)。这类问题现在不处理，后面会越来越难拆。

--end -done

2026-03-19-7

把类型/ABI 检查前移到 sema 和链接阶段，而不是继续依赖“先降到 IR 再看”。现在 NEW/METHOD/GET/SET 仍大量依赖 opaque pointer 和 direct marshal，lowering.cpp (line 877) lowering.cpp (line 924) lowering.cpp (line 985)。WITH 注释写的是“即使异常也会调用 __exit__”，但当前 lowering 只是顺序插入 enter/body/exit，并没有异常控制流，lowering.cpp (line 1089)。这部分最值得补的是统一签名表、参数个数检查、返回值检查和 ABI schema。

--end -done

2026-03-19-8

运行时性能和内存策略。容器运行时目前还是 calloc/realloc/malloc 风格实现，dict 没有扩容/rehash 策略，container_marshal.cpp (line 47) container_marshal.cpp (line 134)；类扩展注册还是固定大小全局数组，也没有线程保护，object_lifecycle.cpp (line 81) object_lifecycle.cpp (line 93)。这部分会影响内存占用、并发安全和长期稳定性，但前提还是前面的主链先收敛。

--end -done

2026-03-19-9

CI 已经覆盖 docs、format、tidy、sanitizer、coverage、benchmark 和多平台构建，ci.yml (line 18)；这点是项目的优点。不过 clang-tidy 现在只扫前 50 个 .cpp，ci.yml (line 87)；.clang-format 仍写的是 c++17，而项目实际是 C++20，.clang-format (line 3) CMakeLists.txt (line 4)；tests/CMakeLists.txt 注释说避免 GLOB_RECURSE，但 integration/benchmark 还是用了 glob，tests/CMakeLists.txt (line 4) tests/CMakeLists.txt (line 168)。

--end -done

2026-03-23-1

更新所有文档。

--end -done

2026-03-23-2

把编译主链拆成明确 stage。现在 driver.cpp 过大，解析、包索引、sema、lowering、link resolution、backend、aux 输出都耦合在一起；polyglot_linker.cpp 和 .ploy sema/lowering 也承担过多职责。建议拆成 frontend -> semantic db -> marshal plan -> bridge generation -> backend -> packaging 六段，每段只产出一个清晰的数据结构。

--end -done

2026-04-09-1 

我已经把图标文件放在了icon文件夹中了，为icon.ico需要你打包给ui的运行程序的图标。

--end -done

2026-04-09-2

1.可以继续把 polyui 启动时窗口标题栏图标（运行时 QIcon）也显式设置为同一个 icon.ico
2.将其他平台的图标也这样应用，如果说ico文件不可以，同目录下有png文件

--end -done

2026-04-09-3

将原来的icon文件夹放在了tools\ui\common\resources中，并修改其他的引用，而且删除原来的icon文件

--end -done

2026-04-09-4

我现在要实现拓扑工具，具体是要向simulink那样可以识别函数的输入与输出，并且可以链接检查输入输出是否符合标准并验证的工具。并且也需要在命令行中实现能打印已有ploy拓扑图的工具。

--end -done

2026-04-09-5

1.ui中的explorer要添加右键菜单，添加常见功能。
2.ui中并没有真正的能生成拓扑图，要真正实现，并且要添加进右键菜单中。

--end -done

2026-04-09-6

- 力导向自动布局（替换简单网格）。
- 通过拖拽进行交互式边创建/删除。
- 源 `.ploy` 文件变更时实时重新加载。
- 断点集成：在调试会话期间高亮当前执行的节点。
- 悬停在端口上时基于工具提示的类型详情检查。

--end -done

2026-04-09-7

要支持拓扑图自动生成poly代码，实现双向互通。

--end -done

2026-04-09-8

拓扑面板高级交互完善：
1. `topology_panel.cpp` 中 `LayoutNodes()` 目前仍是简单网格布局（按 sqrt(n) 列均分），需替换为真实的力导向（Force-Directed）自动布局算法（如 Fruchterman-Reingold 或基于弹簧模型），保证大图不重叠、边长均匀；布局计算完成后需同步更新所有 `TopoEdgeItem` 的贝塞尔路径。
2. 实现通过拖拽进行交互式边创建与删除：在输出端口（`TopoPortItem`，`kOutput`）按下鼠标并拖向输入端口（`kInput`）时，实时绘制临时连线预览，松开后若目标为合法输入端口则创建新 `TopologyEdge` 并同步写回当前 `.ploy` 文件的 `LINK`/`CALL` 声明；右键已有边弹出菜单支持删除，删除后同步移除 `.ploy` 文件中对应的连接语句。
3. 实现源 `.ploy` 文件变更时的实时重新加载：利用 `QFileSystemWatcher` 监听 `current_file_`，文件变更后延迟 200ms（防抖）自动调用 `BuildGraphFromFile()`，并在状态栏提示"Reloaded"。
4. 实现断点集成：`TopologyPanel` 新增 `HighlightExecutingNode(uint64_t node_id)` 与 `ClearExecutionHighlight()` 接口，调试会话中通过 `DebugPanel` 发出信号驱动高亮当前执行节点（节点边框变为亮黄色脉冲动画）；`mainwindow.cpp` 中将 `DebugPanel` 的步进信号连接到该接口。
5. 实现端口悬停工具提示：在 `TopoPortItem::hoverEnterEvent` 中调用 `QToolTip::showText` 显示端口名、类型、所属语言与连接状态（`valid/implicit_convert/incompatible/unknown`）的详细说明。

--end -done

2026-04-09-9

拓扑图与 `.ploy` 代码双向互通：
1. 在 `topology_panel.cpp` 中实现 `GeneratePloySrc(const topo::TopologyGraph &graph) -> std::string`，依据图中节点与边生成合法的 `.ploy` 源代码：每个 `kFunction` 节点生成对应的 `FUNC` 声明，每条边生成 `LINK` 或 `CALL` 语句，`kConstructor` 节点生成 `NEW` 语句，`kPipeline` 节点生成 `PIPELINE` 块；生成的代码需通过 `PloyParser` + `PloySema` 验证可解析、无语义错误，否则在诊断面板报告具体位置。
2. 工具栏新增"Generate .ploy"按钮，点击后将 `GeneratePloySrc` 的输出写入与当前文件同目录的 `<basename>_generated.ploy`，并在编辑器中打开。
3. 在 `polytopo` 命令行工具（`polytopo.cpp`）中新增子命令 `generate`：`polytopo generate <topo.json> -o <output.ploy>`，读取 `topology_printer` 输出的 JSON，通过相同 `GeneratePloySrc` 逻辑生成 `.ploy` 文件，确保 CLI 与 UI 共享同一生成逻辑。
4. 双向同步：当用户在编辑器中手动修改 `.ploy` 文件并保存后，拓扑面板自动重新解析并刷新图形（依赖 `2026-04-09-8` 第3条的 `QFileSystemWatcher`）；当用户在拓扑面板中通过拖拽创建/删除边后，编辑器中对应的 `.ploy` 源码实时高亮变动行。
5. 新增双语文档 `docs/realization/topology_tool.md` / `docs/realization/topology_tool_zh.md`，说明拓扑工具的架构、双向同步协议、`GeneratePloySrc` 的代码生成规则与当前能力边界。

--end -done

2026-04-09-10

跨语言链接主链路闭环（P0 级修复）：
1. `polyc` 在完成 `.ploy` lowering 后，将所有 `CallDescriptor` 和 `LinkEntry` 序列化为二进制格式（与 `2026-02-20-8` 第3条要求一致，aux 目录下 `link_descriptors.bin`），并通过进程间协议（命令行参数 `--ploy-desc <path>`）显式传递给 `polyld`；修改 `tools/polyc/src/driver.cpp`。
2. `polyld` 主流程在调用 `ResolveLinks()` 之前，先读取 `--ploy-desc` 指定文件，调用 `AddCallDescriptor` / `AddLinkEntry` / `AddCrossLangSymbol` 完成注入，保证链接器不空跑；修改 `tools/polyld/src/linker.cpp` 与 `tools/polyld/src/polyglot_linker.cpp`。
3. `polyc` 在 `ResolveLinks()` 返回后，将 `poly_linker.GetStubs()` 返回的桥接 stub 映射为后端可消费的 IR section 与重定位条目，并并入最终输出产物，不再仅写注释；修改 `tools/polyc/src/driver.cpp`。
4. 将集成测试 `compile_pipeline_test.cpp` 中所有手工 `AddCrossLangSymbol()` 替换为走真实主流程（`polyc → aux → polyld`）的测试路径，确保跨语言链接在测试层也是真实闭环而非手工注入。

--end -done

2026-04-09-11

编译管线阶段化重构（driver.cpp 拆分）：
1. 将 `driver.cpp`（当前 2000+ 行）按以下六个阶段拆分为独立模块，每个阶段只接收前一阶段的输出数据结构，禁止跨阶段直接访问全局状态：
   - `stage_frontend`：词法+解析+预处理，产出 AST
   - `stage_semantic`：sema + 类型推断 + 包索引查询，产出语义化 AST + SignatureDB
   - `stage_marshal`：生成 MarshalPlan（跨语言参数转换计划）
   - `stage_bridge`：生成桥接 stub IR（依赖 SignatureDB + MarshalPlan）
   - `stage_backend`：IR 优化（经 PassManager）+ 代码生成，产出 ObjectFile
   - `stage_packaging`：链接 + aux 输出 + 产物打包
2. `PassManager`（已有 `pass_manager.h`）真正接入 `stage_backend`，按 `opt_level` 调用 `Build()` + `RunOnModule()`，不再在 driver 中写死 SSA+Verify 顺序。
3. `--dev` 模式保留降级回退路径（stub 注入、`--force`）；`--release`/CI 默认走 strict 模式，任何阶段失败立即中止并报告阶段名称与错误位置。

--end -done

2026-04-09-12

`.ploy` 类型系统收紧（消除 Any/I64 退化）：
1. 在 `ploy_sema.h` 中将 `strict_mode` 默认值改为 `true`；所有通过 `PloySema` 默认构造的调用点（CLI、UI、测试）均需显式传入 `PloySemaOptions` 以声明意图，不允许隐式走宽松模式。
2. `sema.cpp` 中 30+ 处 `Type::Any()` 回退改为三态：`Concrete`（已知）、`Unknown`（待推断）、`Any`（显式声明可任意）；`Unknown` 类型在边界处（函数参数、返回值、LINK 签名）触发编译错误，要求用户提供显式类型注解或 `INFER` 关键字。
3. `lowering.cpp` 中 `CoreTypeToIR`/`TypeNodeToIR` 遇到未识别类型时改为产生 `DiagError` 并中断 lowering，不再静默回退 `I64`；`GenerateLinkStub` 必须拥有完整的参数与返回类型才能生成，否则报错。
4. IR verifier 引入 `-Werror-placeholder-ir`：在非 `--dev` 模式下，任何 `placeholder`/`Invalid`/未经 sema 认证的 `I64` 占位均视为验证错误，拒绝通过。

--end -done

2026-04-09-13

测试质量提升（消除防崩溃断言）：
1. 将以下文件中所有 `REQUIRE(true)` 替换为行为级断言，并补充失败路径测试用例：
   - `tests/unit/middle/optimization_passes_test.cpp`：校验循环变换前后 IR 指令计数变化与等价性
   - `tests/unit/middle/lto_test.cpp`：校验跨模块内联后符号消解数量与导出表一致性
   - `tests/unit/runtime/gc_algorithms_test.cpp`：校验存活对象数、回收量、暂停时间上界
   - `tests/unit/runtime/threading_services_test.cpp`：校验任务完成数、竞争条件检测、超时与资源回收
   - `tests/unit/frontends/ploy/devirtualization_test.cpp` / `loop_optimization_test.cpp`：校验变换前后调用图变化
2. 新增端到端失败路径测试：参数数量错误、类型不兼容、未注册符号等场景均需有对应的 `REQUIRE_THROWS` 或诊断消息断言，确保错误信息可读且定位准确。
3. `tests/integration/` 下新增至少一个完整的 compile → link → run → 校验输出的端到端测试，走真实 `polyc` 可执行文件（不模拟），覆盖 C++/Python 跨语言最小示例。

--end -done

2026-04-09-14

运行时容器与对象生命周期完善：
1. `container_marshal.cpp` 中 `list → pylist` 和 `dict → pydict` 转换实现真实的内存布局转换（CPython `PyList_New`/`PyDict_New` + 逐元素 marshal），而非直接返回原指针；同时实现 `__ploy_rt_convert_cppvec_to_list` 与 `__ploy_rt_convert_list_generic` 两个 runtime 导出符号，消除链接器对这两个符号的悬空引用。
2. `object_lifecycle.cpp` 中类扩展注册由固定大小全局数组改为 `std::vector` + `std::shared_mutex`（读写锁），保证并发注册与查询安全。
3. `dict` 实现开放寻址哈希表 + 负载因子 0.75 时自动 rehash，删除旧的 `calloc`/`realloc` 线性增长策略。
4. 将上述改动补充到 `runtime_abi.md` / `runtime_abi_zh.md` 文档中，说明转换协议与生命周期所有权规则。

--end -done

2026-04-09-15

gui中语法高亮的问题，现在编译出来的没有gui中没有poly语言的高亮，请添加

--end -done

2026-04-09-16

1.gui中编辑区的语法高亮还是没有解决，所有主题所有语言都没有语法高亮。
2.拓扑图的链接不够动态，当移动一个块的时候线条没有跟随。
3.gui提供模板创建，包括支持的语言模板（cpp，python等）。    ·   `

--end -done

2026-04-09-17

1.语法高亮还没有被修复，想办法修复；
2.设置中的 show toolbar 和show status bar 与 toggle explorer 默认为true

--end -done

2026-04-09-18

浅色主题还有黑色，比如explorer，选中行，还有上端tools bar等部分还是黑色的。

--end -done

2026-04-09-19

poly的语言要支持类型推断，要智能的类型推断，避免出现更多的编译错误。

--end -done

2026-04-10-01

在windows上编译sample时会出现：polyld: link failed
  Not a Mach-O file: a.out.pobj
  Failed to load object file: a.out.pobj
  Failed to load object files
在macos上：编译sample时会出现[polyc] Staged compilation pipeline (.ploy)... [pipeline] link descriptors -> /Volumes/extend/PolyglotCompiler/tests/samples/01_basic_linking/aux/basic_linking_link_descriptors.paux
sh: polyld: command not found
这样的错误，请修复polyld link failed 的错误。

--end -done

2026-04-10-02

在关闭polyui的时候有如下的报错信息，在文件crash.txt中，请分析原因并修复

--end -done

2026-04-10-03

关于拓扑图与整个poly中的类型推断问题，c++这种显式有return与参数的类型的需要从程序中读取，但是对于python不需要显式声明的需要进行类型推断，但是由于python中可以标注返回类型与参数类型，也要从程序中读取。同理，也要支持java；dotnet；rust等语言。

--end -done

2026-04-10-04

按照Doxygen格式给全部模块的代码添加或整理注释。

--end -done

2026-04-10-05

1.这是当在拓扑图里面链接后生成的代码，并不符合poly代码规范：LINK cpp::math_ops::multiply.return -> python::string_utils::format_result.int
2.拓扑图逻辑有问题，他不仅显示了poly代码中的LINK语句，连接到FUNC上的逻辑很奇怪，请修改。
3.拓扑图里面每一块都需要有右键菜单。

--end -done

2026-04-10-06

1.比如sample01的cpp中
int abs_val(int x) {
    return x < 0 ? -x : x;
}
这样的代码标定了返回值是int，但是在拓扑图中是unknown，请修改这种bug

2.拓扑图的FUNC和PIPLINE的逻辑还是不对，继续修改

--end -done

2026-04-10-07

我的意思是那些link的内容不应该是在func或者pipline中的内容吗？为什么他们会显示在同一个层级？
所以是不是应该把link和func的call分开呢？因为在流程中是func之间的链接或者是pipline中func的链接，之后被func调用pipline等逻辑，现在你都显示在一个层级是不是不合理？
所以是不是应该出现一个选项是产生link的逻辑还是func之间的逻辑？

--end -done

2026-04-10-08

1.PIPLINE的拓扑图逻辑也要按照前述的逻辑进行修改，PIPLINE中要显示FUNC顺序逻辑与数据流向。
2.gui的初始视图进去只有编辑器，其他的部分Explorer，toolbar等元素都没有，这个bug请修改。

--end -done

2026-04-11-01

PIPELINE的显示逻辑还是不对，PIPELINE内部应该只显示PIPELINE的FUNC的调用，但是FUNC内部的调用需要双击FUNC才能看见。其他部分也需要这样。

--end -done

2026-04-11-02

为该行为添加 UI 提示（节点 hover-tip 说明双击可展开），并在节点 header 加入小「▶/▼」图标指示展开状态。
添加单元/集成测试覆盖：加载一个包含 PIPELINE 的样例，断言默认 PIPELINE 视图中只有 stage 节点可见，双击后内部 CALL 边与节点可见。

--end -done

2026-04-11-03

PIPELINE双击的逻辑不对，不应该在同一个窗口里面显示，应该在新的子窗口中出现。

--end -done

2026-04-11-04

全项目分析：优化方向与后续需求规划
====================================

以下是对全项目（369 个源文件 / ~133K 行 C++20）的系统性审计结论，按优先级从高到低排列。

─── 方向一：拓扑子窗口递归钻入与交互闭环 ───

当前 DrillDownWindow 的 TopoGraphicsView 没有设置 Panel 指针，因此子窗口中的节点无法继续递归钻入（双击无反应），也无法通过右键菜单操作。子窗口中也缺少视图模式选择、节点详情面板、力导向布局等主面板已有的功能。

实现内容：
1. DrillDownWindow 中为 TopoGraphicsView 设置一个轻量代理 Panel（或直接把 parent_panel_ 传入），使子窗口中的 expandable 节点也可以递归打开新的子窗口（嵌套钻入）。
2. 子窗口添加力导向布局支持（复用 TopologyPanel 的力导向算法），替换当前简单网格。
3. 子窗口中的节点支持右键菜单（Go to Source、Show Details、Highlight Connections），复用主面板的 contextMenuEvent 逻辑。
4. 子窗口增加节点选中时的详情侧边栏（端口类型、源码位置等），与主面板的 details_tree_ 行为一致。
5. 为递归钻入添加面包屑导航栏（breadcrumb），显示 Pipeline > Stage > SubCall 的钻入路径，支持点击返回上层。
6. 编写单元测试：验证 DrillDownWindow 可正确创建，场景中包含期望的节点/边数，验证重复打开同一节点会复用窗口而非重复创建。

--end -done

2026-04-11-05

─── 方向二：polyld 跨语言链接器 marshal 函数仍有大量 (void) 占位 ───

polyglot_linker.cpp 中 EmitContainerMarshal、EmitReturnMarshal、EmitCallingConventionAdaptor 等函数接收了 from_lang、to_lang、param_idx 等参数但用 (void) 静默丢弃，意味着这些 stub 生成路径尚未按语言/ABI 差异生成真实的转换代码。虽然上层 GenerateX86_64Stub/GenerateARM64Stub 已有基础实现，但 marshal 层仍是"通用路径"，未考虑 Python↔C++ 的 GIL 获取/释放、Rust 的 borrow 语义标注、Java 的 JNI 调用约定。

实现内容：
1. EmitContainerMarshal：按 from_lang/to_lang 组合（cpp↔python、cpp↔rust 等）生成实际的容器深拷贝/视图共享代码，而非空操作。至少实现 cpp::vector↔python::list 双向路径。
2. EmitReturnMarshal：根据返回类型和目标语言 ABI 生成 box/unbox 指令（如 Python int → C++ int64_t）。
3. EmitCallingConventionAdaptor：区分 System V vs MSVC ABI，生成寄存器重排代码。
4. Python 桥接 stub 中插入 GIL 获取/释放指令（PyGILState_Ensure/PyGILState_Release）。
5. 为以上每种路径编写集成测试（从 .ploy 编译到 stub 生成，验证 stub 字节码包含期望的指令模式）。

--end -done

2026-04-11-06

─── 方向三：中端 IR 到后端的优化集成仍有断层 ───

PassManager（pass_manager.h）已实现 O0-O3 四级管线构建，但 polyc 的 stage_backend.cpp 只在 Release 模式下真正调用 PassManager::RunOnModule()。更重要的是，PGO 和 LTO 管线虽然代码量很大（link_time_optimizer.cpp ~2000 行、profile_data.cpp ~300 行），但与 polyc 主编译流程的接入路径不完整：polyc 没有 --pgo-generate/--pgo-use 命令行选项，LTO 在非 --lto 模式下完全不触发。

实现内容：
1. polyc 新增 `--pgo-generate` 和 `--pgo-use <profile.pgd>` 命令行选项，在 stage_backend 中条件性插入 PGO 插桩/利用 pass。
2. polyc 的 `--lto` 选项在 stage_packaging 中真正调用 LinkTimeOptimizer 的跨模块 inline/DCE/type merge，而非仅做文件拷贝。
3. `polyopt` 工具现在只优化空 IRContext（optimizer.cpp line 88），需要真正解析输入 .ir 文件（使用 IRParser），跑 PassManager，然后输出优化后的 IR。
4. 为 O1/O2/O3 各级别编写回归测试：输入带有可优化模式的 IR（如常量折叠、死代码消除、公共子表达式），验证输出 IR 的指令数或特定指令消失。

--end -done

2026-04-11-07

─── 方向四：后端对象文件格式完善 ───

ELF builder（object_file.cpp）的 e_machine 已按架构切换，但 Mach-O builder 仍然只写最小头部。COFF/PE 加载路径在 linker.cpp 中有框架但不完整（Windows 产物在本地测试中使用 .pobj 私有格式而非标准 COFF）。debug_emitter.cpp 中 PDB TPI 类型流为空（line 1061）、.eh_frame FDE 简化（line 894），这些直接影响 Windows 调试体验。

实现内容：
1. Mach-O builder：补齐 LC_SYMTAB、LC_DYSYMTAB、__TEXT/__DATA segment/section 布局，使 macOS 上 polyc 产物可被 ld64 或 lld 消费。
2. COFF builder：实现标准 COFF object 输出（IMAGE_FILE_HEADER + section table + relocations），在 Windows 上取代 .pobj 格式，使 polyld 或 MSVC link.exe 可消费。
3. PDB TPI stream：至少为基本类型（int/float/double/pointer）和函数签名生成有效的 type record，使 Visual Studio/WinDbg 能显示变量类型。
4. .eh_frame：生成标准 CIE + FDE，使 Linux 上的异常展开和 GDB backtrace 可用。
5. 对应的集成测试：编译一个最小 C++ 源码到 .o，用 readelf/objdump/dumpbin 验证输出格式有效。

--end -done

2026-04-11-08

─── 方向五：UI/polyui 编辑器体验优化 ───

polyui 的编辑器层（code_editor.cpp / syntax_highlighter.cpp）已支持基础语法高亮和暗/亮主题，但以下交互优化可显著提升开发体验：

实现内容：
1. 代码自动补全：为 .ploy 语言实现关键字补全（54 个关键字 + 内置类型），在用户输入时弹出补全列表；C++/Python/Rust 文件提供基于当前文件词法分析的标识符补全。
2. 错误波浪线：将 Diagnostics 中的 error/warning 位置映射到编辑器文本，在对应区域绘制红色/橙色波浪下划线（类似 VS Code 的 squiggly lines），而非仅在 Output Panel 输出文字。
3. 行内诊断提示：在错误行右侧灰色显示简短错误消息（inline diagnostic），减少用户切换到 Output 面板的频率。
4. 跳转到定义：在 .ploy 文件中 Ctrl+Click 一个 FUNC/PIPELINE 名称时，跳转到其声明位置；跨文件时跳转到对应的 .cpp/.py/.rs 文件。
5. 迷你地图（Minimap）：在编辑器右侧绘制整个文件的缩略视图，支持点击快速导航。

--end -done

2026-04-11-09

─── 方向六：测试体系深化 ───

当前 808 个 test case 覆盖了大部分模块，但存在明显薄弱区：
- topology_test.cpp：22 个 case 只测试 TopologyGraph/Analyzer/Validator/Printer，没有 UI 层测试（DrillDownWindow、场景构建、视图模式切换）。
- 无 polyc 端到端 CLI 测试：没有测试从 .ploy 源码经 polyc 编译到产物再 polyld 链接的完整路径。
- 无 polyui 冒烟测试：没有 QTest 框架的 GUI 测试。
- backends/ 测试只有 61 个 case，其中 WASM 后端缺乏独立测试。

实现内容：
1. 新增 polyc CLI 端到端测试：用 QProcess 或 std::system 调用 polyc 编译 samples/01_basic_linking，验证退出码和产物文件存在性。
2. 新增 WASM 后端独立测试：构造最小 IR（单函数、加法），调用 WasmTarget::EmitAssembly()，验证输出 WAT 包含 (func 和 (export。
3. 新增 topology UI 测试：实例化 TopologyPanel，调用 BuildGraphFromFile() 加载 sample，验证 node_items_ 和 edge_items_ 数量，模拟 OpenDrillDownWindow 调用验证子窗口创建。
4. 新增 .ploy sema strict mode 回归测试：验证类型错误在 strict 模式下被拒绝，在 non-strict 模式下降级为 warning。
5. 将所有新增测试纳入 CTest，目标新增 30+ case 使总数超过 840。

--end -done

2026-04-11-10

─── 方向七：文档同步与完善 ───

docs/ 已有完善的双语文档体系（specs/realization/tutorial/api 各有 _zh 版本），但以下内容需更新：
- topology_tool.md/topology_tool_zh.md：未记录子窗口钻入机制（2026-04-11-03 新增的 DrillDownWindow）。
- USER_GUIDE 和 api_reference：测试统计数字（808 cases / 3 suites）可能已过时（实际 CTest 有 20 个 suite），需对齐。
- README.md 的 Last Updated 仍为 2026-03-19，需更新。

实现内容：
1. 更新 topology_tool.md / topology_tool_zh.md：新增「子窗口钻入」章节，说明 DrillDownWindow 的架构、交互流程、面包屑导航（如已实现）。
2. 更新 USER_GUIDE.md / USER_GUIDE_zh.md 和 README.md 中的测试统计数据。
3. 更新 README.md 的 Last Updated 日期。
4. 在 api_reference.md / api_reference_zh.md 中新增 DrillDownWindow 类的 API 说明。
5. 运行 docs_sync_check.py --scope core 确认中英文核心文档同步无 drift。

--end -done

2026-04-14-1

功能扩张（优先交付）

1.polyui 编辑器增强：
- 为 `.ploy` 增加完整关键字与内置类型智能补全，并支持函数名、PIPELINE 名、LINK 目标符号补全；
- 为 C++/Python/Rust/Java/.NET 文件提供基于当前工程符号索引的补全；
- 实现 diagnostics 波浪线（error=红色，warning=橙色）与行内诊断提示（inline diagnostics）；
- 支持 Ctrl+Click 跳转定义（先支持 `.ploy` 内部，再支持跨语言源文件）；
- 增加模板中心（New From Template）：cpp/python/rust/java/dotnet/ploy 六类模板，支持从 Explorer 右键创建。

2.拓扑工具能力扩张：
- 增加视图模式切换：`LINK 关系视图` 与 `FUNC/PIPELINE 调用数据流视图`，两种模式可独立筛选与导出；
- 增加拓扑节点分组（按 language、pipeline、module）与折叠/展开；
- 增加边与节点的批量操作（多选删除、多选高亮、多选导出）；
- 拓扑图导出的 `.ploy` 代码需要保留 source location 注释，便于回写和差异对比；
- CLI `polytopo` 增加 `--view-mode link|call` 和 `--filter-language <lang>` 参数，与 UI 行为一致。

3.编译体验与产物可观测性扩张：
- `polyc` 输出阶段级进度（frontend/sema/marshal/bridge/backend/packaging）以及每阶段耗时、峰值内存；
- 增加 `--progress=json` 输出机器可读进度事件，供 UI 进度条实时消费；
- 增加增量编译缓存（按输入哈希和编译参数命中），并支持 `--clean-cache`；
- `aux` 目录新增 `build_profile.bin`（二进制）记录阶段统计，禁止输出明文占位统计；
- 编译失败时输出错误汇总（按 error code 聚合）并附带首个 traceback。

4.插件与扩展生态扩张：
- 插件 API 增加编辑器扩展点：补全提供器、诊断提供器、模板提供器、拓扑后处理器；
- 插件加载支持版本约束（min/max host version）和冲突检测；
- UI 增加插件管理页：启用/禁用、加载顺序、崩溃隔离与日志查看；
- 增加插件沙箱策略：超时、内存上限、失败自动熔断。

5.测试与验收要求：
- 新增端到端测试覆盖上述扩张能力：编辑器补全、波浪线、拓扑双视图、polyc 进度事件、缓存命中；
- 所有新增测试不得使用 `REQUIRE(true)`；必须包含行为断言和失败路径断言；
- 新增功能需在 macOS 与 Linux 下通过构建和测试，Windows 至少通过编译与 smoke test。

6.文档同步要求：
- 必须同步更新 `README.md`、`docs/USER_GUIDE.md`、`docs/USER_GUIDE_zh.md`；
- 新增双语实现文档：`docs/realization/editor_expansion.md` 与 `docs/realization/editor_expansion_zh.md`；
- 文档中需要包含能力边界、配置项、命令示例、常见失败场景与排查步骤。

--end -done

2026-04-20-1

现在的ui的默认界面仍然只是有编辑器，还是需要我手动打开文件资源管理器等。请修改这个bug。

--end -done

2026-04-20-2

增加更多的拓扑图布局算法，并且尽量默认的算法是静态的。

--end -done

2026-04-20-3

修复下列ci错误

1.check formatting fail:

Run find common middle frontends backends runtime tools \
  find common middle frontends backends runtime tools \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
    -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
    | xargs -0 -r clang-format-20 --dry-run --Werror --style=file \
    2>&1 | head -200
  # Exit with the pipeline status
  find common middle frontends backends runtime tools \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
    -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
    | xargs -0 -r clang-format-20 --dry-run --Werror --style=file
  shell: /usr/bin/bash -e {0}
common/src/debug/dwarf5.cpp:9:1: error: code should be clang-formatted [-Wclang-format-violations]
#include "common/include/debug/dwarf5.h"
^
common/src/debug/dwarf5.cpp:20:51: error: code should be clang-formatted [-Wclang-format-violations]
void LocationExpr::AddOp(Op op, int64_t operand) {
                                                  ^
common/src/debug/dwarf5.cpp:24:52: error: code should be clang-formatted [-Wclang-format-violations]
std::vector<uint8_t> LocationExpr::Encode() const {
                                                   ^
common/src/debug/dwarf5.cpp:25:33: error: code should be clang-formatted [-Wclang-format-violations]
    std::vector<uint8_t> result;
                                ^
common/src/debug/dwarf5.cpp:27:20: error: code should be clang-formatted [-Wclang-format-violations]
    for (const auto& [op, operand] : operations_) {
                   ^
common/src/debug/dwarf5.cpp:27:21: error: code should be clang-formatted [-Wclang-format-violations]
    for (const auto& [op, operand] : operations_) {
                    ^
common/src/debug/dwarf5.cpp:27:52: error: code should be clang-formatted [-Wclang-format-violations]
    for (const auto& [op, operand] : operations_) {
                                                   ^
common/src/debug/dwarf5.cpp:28:52: error: code should be clang-formatted [-Wclang-format-violations]
        result.push_back(static_cast<uint8_t>(op));
                                                   ^
common/src/debug/dwarf5.cpp:30:33: error: code should be clang-formatted [-Wclang-format-violations]
        // Add operand if needed
                                ^
common/src/debug/dwarf5.cpp:31:22: error: code should be clang-formatted [-Wclang-format-violations]
        switch (op) {
                     ^
common/src/debug/dwarf5.cpp:32:29: error: code should be clang-formatted [-Wclang-format-violations]
            case Op::kFBReg:
                            ^
common/src/debug/dwarf5.cpp:33:31: error: code should be clang-formatted [-Wclang-format-violations]
            case Op::kConst1u:
                              ^
common/src/debug/dwarf5.cpp:34:65: error: code should be clang-formatted [-Wclang-format-violations]

2.static analysis fail:

Run # Run on all project source files (exclude tests, deps, .cache)
  # Run on all project source files (exclude tests, deps, .cache)
  find common middle frontends backends runtime tools \
    -type f -name '*.cpp' -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
    | xargs -0 -r -P$(nproc) -n 1 \
      clang-tidy-17 -p build --warnings-as-errors='bugprone-use-after-move,bugprone-dangling-handle' \
    > clang-tidy.log 2>&1
  tail -100 clang-tidy.log
  shell: /usr/bin/bash -e {0}
Error: Process completed with exit code 123.

3.Sanitizers (ASan + UBSan) fail:

Run cd build
Test project /home/runner/work/PolyglotCompiler/PolyglotCompiler/build
      Start  1: test_core
 1/17 Test  #1: test_core ........................   Passed    0.10 sec
      Start  2: test_plugins
 2/17 Test  #2: test_plugins .....................   Passed    0.03 sec
      Start  3: test_frontend_common
 3/17 Test  #3: test_frontend_common .............   Passed    0.08 sec
      Start  4: test_frontend_python
 4/17 Test  #4: test_frontend_python .............   Passed    0.28 sec
      Start  5: test_frontend_cpp
 5/17 Test  #5: test_frontend_cpp ................   Passed    0.05 sec
      Start  6: test_frontend_rust
 6/17 Test  #6: test_frontend_rust ...............   Passed    0.10 sec
      Start  7: test_frontend_ploy
 7/17 Test  #7: test_frontend_ploy ...............   Passed    0.29 sec
      Start  8: test_frontend_java
 8/17 Test  #8: test_frontend_java ...............   Passed    0.05 sec
      Start  9: test_frontend_dotnet
 9/17 Test  #9: test_frontend_dotnet .............   Passed    0.05 sec
      Start 10: test_middle
10/17 Test #10: test_middle ......................   Passed    0.11 sec
      Start 11: test_backends
11/17 Test #11: test_backends ....................   Passed    0.05 sec
      Start 12: test_runtime
12/17 Test #12: test_runtime .....................***Failed    0.25 sec
/home/runner/work/PolyglotCompiler/PolyglotCompiler/tests/unit/runtime/polyrt_test.cpp:230:9: runtime error: load of misaligned address 0x53100001ee1e for type 'long unsigned int', which requires 8 byte alignment
0x53100001ee1e: note: pointer points here
 00 00 00 00 00 00  00 00 00 00 00 00 01 ec  07 00 00 00 00 00 00 c4  17 00 00 00 00 00 00 00  00 00
             ^ 
    #0 0x55d4f73290c2 in CATCH2_INTERNAL_TEST_28 /home/runner/work/PolyglotCompiler/PolyglotCompiler/tests/unit/runtime/polyrt_test.cpp:230
    #1 0x55d4f75bba4c in invoke src/catch2/internal/catch_test_registry.cpp:58
    #2 0x55d4f757eea1 in Catch::TestCaseHandle::invoke() const src/catch2/../catch2/catch_test_case_info.hpp:116
    #3 0x55d4f7577ff8 in Catch::RunContext::invokeActiveTestCase() src/catch2/internal/catch_run_context.cpp:554
    #4 0x55d4f75767b0 in Catch::RunContext::runCurrentTest(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&) src/catch2/internal/catch_run_context.cpp:517
    #5 0x55d4f75696cc in Catch::RunContext::runTest(Catch::TestCaseHandle const&) src/catch2/internal/catch_run_context.cpp:239
    #6 0x55d4f7a87f6c in execute src/catch2/catch_session.cpp:111
    #7 0x55d4f7a8f737 in Catch::Session::runInternal() src/catch2/catch_session.cpp:333
    #8 0x55d4f7a8d606 in Catch::Session::run() src/catch2/catch_session.cpp:264
    #9 0x55d4f7885b66 in int Catch::Session::run<char>(int, char const* const*) src/catch2/../catch2/catch_session.hpp:41
    #10 0x55d4f78858c2 in main src/catch2/internal/catch_main.cpp:36
    #11 0x7f06ba42a1c9  (/lib/x86_64-linux-gnu/libc.so.6+0x2a1c9) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #12 0x7f06ba42a28a in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x2a28a) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #13 0x55d4f727a344 in _start (/home/runner/work/PolyglotCompiler/PolyglotCompiler/build/tests/test_runtime+0x1186344) (BuildId: c6a5fd47173762ca54ad8f45019ddc4a0db24085)


      Start 13: test_linker
13/17 Test #13: test_linker ......................   Passed    0.04 sec
      Start 14: test_topology
14/17 Test #14: test_topology ....................   Passed    0.08 sec
      Start 15: test_e2e
15/17 Test #15: test_e2e .........................   Passed    0.15 sec
      Start 16: unit_tests
16/17 Test #16: unit_tests .......................***Failed    1.10 sec
Randomness seeded to: 3584451927
[plugin-manager] Failed to load /nonexistent/path/libpolyplug_fake.so: /nonexistent/path/libpolyplug_fake.so: cannot open shared object file: No such file or directory
[plugin-manager] Registered file type: .xyz -> xyz-lang (from test.plugin)
[plugin-manager] Registered file type: .xyz -> xyz-lang-v2 (from test.plugin2)
[plugin-manager] Registered file type: .abc -> abc-lang (from test.plugin)
[python-lowering] unresolved type hint 'A'; defaulting to i64
[python-lowering] unresolved type hint 'A'; defaulting to i64
/home/runner/work/PolyglotCompiler/PolyglotCompiler/tests/unit/runtime/polyrt_test.cpp:230:9: runtime error: load of misaligned address 0x53100001ee1e for type 'long unsigned int', which requires 8 byte alignment
0x53100001ee1e: note: pointer points here
 00 00 00 00 00 00  00 00 00 00 00 00 01 ec  07 00 00 00 00 00 00 c4  17 00 00 00 00 00 00 00  00 00
             ^ 
    #0 0x555af2ead13a in CATCH2_INTERNAL_TEST_28 /home/runner/work/PolyglotCompiler/PolyglotCompiler/tests/unit/runtime/polyrt_test.cpp:230
    #1 0x555af43851ca in invoke src/catch2/internal/catch_test_registry.cpp:58
    #2 0x555af434bc0f in Catch::TestCaseHandle::invoke() const src/catch2/../catch2/catch_test_case_info.hpp:116
    #3 0x555af4344f62 in Catch::RunContext::invokeActiveTestCase() src/catch2/internal/catch_run_context.cpp:554
    #4 0x555af434371a in Catch::RunContext::runCurrentTest(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&) src/catch2/internal/catch_run_context.cpp:517
    #5 0x555af4336636 in Catch::RunContext::runTest(Catch::TestCaseHandle const&) src/catch2/internal/catch_run_context.cpp:239
    #6 0x555af4d245e8 in execute src/catch2/catch_session.cpp:111
    #7 0x555af4d2bdb3 in Catch::Session::runInternal() src/catch2/catch_session.cpp:333
    #8 0x555af4d29c82 in Catch::Session::run() src/catch2/catch_session.cpp:264
    #9 0x555af45752bc in int Catch::Session::run<char>(int, char const* const*) src/catch2/../catch2/catch_session.hpp:41
    #10 0x555af4575018 in main src/catch2/internal/catch_main.cpp:36
    #11 0x7f645902a1c9  (/lib/x86_64-linux-gnu/libc.so.6+0x2a1c9) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #12 0x7f645902a28a in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x2a28a) (BuildId: 8e9fd827446c24067541ac5390e6f527fb5947bb)
    #13 0x555af23f29d4 in _start (/home/runner/work/PolyglotCompiler/PolyglotCompiler/build/tests/unit_tests+0x64909d4) (BuildId: 102ab695825a78cb1a49d085aac2f4ea25f4065f)


      Start 17: integration_tests
17/17 Test #17: integration_tests ................   Passed    0.57 sec

88% tests passed, 2 tests failed out of 17

Label Time Summary:
backend     =   0.05 sec*proc (1 test)
core        =   0.13 sec*proc (2 tests)
e2e         =   0.15 sec*proc (1 test)
frontend    =   0.88 sec*proc (7 tests)
linker      =   0.04 sec*proc (1 test)
middle      =   0.11 sec*proc (1 test)
runtime     =   0.25 sec*proc (1 test)
topology    =   0.08 sec*proc (1 test)
unit        =   1.69 sec*proc (15 tests)

Total Test time (real) =   3.37 sec

The following tests FAILED:
Errors while running CTest
	 12 - test_runtime (Failed)                             runtime unit
	 16 - unit_tests (Failed)
Error: Process completed with exit code 8.

4.Code Coverage fail:

Run lcov --capture --directory build --output-file coverage.info \
Capturing coverage data from build
geninfo cmd: '/usr/bin/geninfo build --output-filename coverage.info --ignore-errors mismatch --ignore-errors empty --memory 0'
Found gcov version: 13.3.0
Using intermediate gcov format
Writing temporary data to /tmp/geninfo_datLHC_
Scanning build for .gcda files ...
Found 308 data files in build
Processing build/tests/CMakeFiles/test_frontend_cpp.dir/unit/constexpr_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
Processing build/tests/CMakeFiles/test_frontend_cpp.dir/unit/frontends/cpp/parser_basic_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_cpp.dir/unit/frontends/cpp/sema_member_overload_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/basic_string.h:355: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_cpp.dir/unit/frontends/cpp/parser_advanced_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/ext/atomicity.h:52: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_topology.dir/unit/tools/topology_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/ext/atomicity.h:52: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_common.dir/unit/frontends/frontend_registry_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/alloc_traits.h:482: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_common.dir/unit/frontend/preprocessor_tests.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/import_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/phi_and_control_flow_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/e2e_python_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/lowering_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/sema_attribute_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/lexer_fstring_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/sema_scope_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/advanced_features_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/ext/atomicity.h:52: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/parser_basic_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/test_frontend_python.dir/unit/frontends/python/parser_advanced_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/integration_tests.dir/integration/performance/perf_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/integration_tests.dir/integration/e2e/polyc_e2e_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/alloc_traits.h:482: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/integration_tests.dir/integration/interop_tests/interop_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/integration_tests.dir/integration/object_format_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/ext/alloc_traits.h:98: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/integration_tests.dir/integration/compile_tests/compile_pipeline_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/basic_string.h:355: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/unit_tests.dir/unit/linker_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/stl_iterator_base_funcs.h:106: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/unit_tests.dir/unit/tools/topology_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/ext/alloc_traits.h:98: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/unit_tests.dir/unit/runtime/polyrt_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/unit_tests.dir/unit/runtime/ffi_interop_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/new_allocator.h:88: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
Processing build/tests/CMakeFiles/unit_tests.dir/unit/runtime/threading_services_test.cpp.gcda
geninfo: WARNING: /usr/include/c++/13/bits/allocator.h:163: unexecuted block on non-branch line with non-zero hit count.  Use "geninfo --rc geninfo_unexecuted_blocks=1 to set count to zero.
	(use "geninfo --ignore-errors gcov,gcov ..." to suppress this warning)
geninfo: ERROR: Unexpected negative count '-31165' for /home/runner/work/PolyglotCompiler/PolyglotCompiler/runtime/include/services/threading.h:438.
	Perhaps you need to compile with '-fprofile-update=atomic
	(use "geninfo --ignore-errors negative ..." to bypass this error)
Error: Process completed with exit code 1.

--end -done

2026-04-26-01

前端增加更多语言：js，Ruby，Go三种语言。

--end -done

2026-04-26-02

1.mimalloc 被声明但从未被任何目标链接或 #include（孤儿依赖），需要改为真正使用这个高性能库。
2.fmt 同样是声明而未被使用，需要真正使用 fmt 替换 std::ostringstream/printf

--end -done

2026-04-26-03

背景：经过对全项目代码的审查，发现 polyc 在编译用户源码时，对各语言里 `import / #include / use / using` 等"外部包引用"语句目前只做到"语法识别 + 占位符号登记"，并未真正从外部包加载符号声明，也未把外部包的实现链接进最终产物。本次需求要求把"外部包消费"这条链路真正打通，使 polyc 编译出的程序能够引用并链接真实的第三方库。具体要求如下：

1.C++ 前端：把 `frontends/common/src/preprocessor.cpp`（已具备 `Define / AddIncludePath / SetIncludePaths / #if 条件求值` 等能力）真正接到 C++ 前端的 token 流上，替换现有 `parser.cpp` 中"直接跳过 kPreprocessor 整行"的逻辑，实现 `#include "x.h"`、`#include <vector>`、`#define`、`#ifdef` 等指令的真实展开；支持通过 polyc CLI 的 `--I=<path>` 注入头文件搜索路径，并新增 `-isystem <path>`、`-D<name>[=<value>]`、`-U<name>` 三类选项。

2.Python 前端：把 `frontends/python/src/sema/sema.cpp` 中的 `BuiltinModuleExports()` 硬编码表替换为基于 `.pyi` 存根的真实类型加载器；至少支持读取 typeshed（标准库存根）与用户通过 `--python-stubs=<dir>` 提供的第三方包存根；对于在已发现的 site-packages 中找到的 `.pyi`/`__init__.pyi`，自动加入搜索路径。Lowering 阶段对非内置模块的导入应生成可被运行时实际解析的调用，而不是裸 `__py_import_from` 占位。

3.Java 前端：实现完整可用的 `.class` / `.jar` 元数据读取器（解析常量池、方法表、字段表、签名属性），让 `import java.util.List;` 能够把真实类型与方法签名引入符号表；polyc 新增 `--classpath=<paths>` / `-cp <paths>` CLI，多个路径用平台分隔符。`module-info.class` 也应被读取以提供模块级可见性信息。

4 .NET 前端：实现 ECMA-335 metadata 读取器（至少读取 `#Strings`、`#US`、`#GUID`、`#Blob` 堆与 `TypeDef`、`MethodDef`、`MemberRef`、`AssemblyRef` 表），让 `using System.Collections.Generic;` 能将 `List<T>` 等真实类型/方法引入符号表；polyc 新增 `--reference=<dll>` / `-r <dll>` CLI，可指定多个程序集；自动从 `dotnet --list-runtimes` 给出的路径与 NuGet 全局缓存中查找。

5.Rust 前端：补齐 `cargo metadata --format-version 1` 调用并把结果写入 `PackageIndexer`；对 `use ::crate_name::...` 路径，从 `target/<profile>/deps/*.rmeta` 与 `cargo metadata` 给出的 manifest_path 解析依赖图，按 crate 名注入符号表；polyc 新增 `--crate-dir=<dir>` 与 `--extern <name>=<path>` CLI。

6.Ploy 包发现层：补齐 `PackageIndexer` 缺失的 cargo 实现，与 Python/Java/.NET/已有逻辑保持同样的"manager + 路径 + 缓存 key"三元组结构；并把 `PackageInfo` 中收集到的 `location/version` 真正下发到 lowering 阶段，使 `IMPORT python PACKAGE numpy::(array, mean);` 等语法不仅在 sema 通过校验，还能在 IR 中产生引用真实包符号的调用。

7.链接器与运行时：扩展 `polyld` 与 `runtime/src/libs/*_rt.c`，对各前端在 lowering 中产生的"外部包符号引用"在加载阶段通过 dlopen/LoadLibrary + dlsym/GetProcAddress 真正解析；为每种语言提供对应的 ABI 适配桥（Python 走 CPython C-API、Java 走 JNI、.NET 走 hostfxr/CoreCLR、Rust 走 C ABI cdylib、C++ 走系统 ABI）。当宿主机缺失对应运行时（如未装 Python）时，polyc 应给出明确的诊断而非崩溃。

8.polyc CLI 统一：在 `tools/polyc/src/driver.cpp` 中加入第 1–5 条提到的 CLI 选项，并在 `--help` 中归类显示；同步更新 `tools/polyc/include/compilation_pipeline.h` 与 `stage_frontend.cpp / stage_semantic.cpp` 的 `Settings` 结构。

9.测试：为 1–7 中每一项都增加端到端测试，放在 `tests/integration/external_packages/`，覆盖：
  - `cpp/include_real_header.cpp` — 真正消费一个项目内放置的 `.h` 头文件。
  - `python/import_numpy_stub.py` — 通过用户提供的 `.pyi` 存根消费一个伪 `numpy.array` 签名。
  - `java/import_jar_class.java` — 通过 `--classpath` 消费一个由测试夹具生成的最小 `.jar`。
  - `dotnet/use_dll_reference.cs` — 通过 `--reference` 消费一个最小 `.dll`。
  - `rust/use_extern_crate.rs` — 通过 `--extern` 消费一个最小 rmeta。
  - `ploy/package_real_call.ploy` — 让 ploy 的 PACKAGE 导入真正落到上述五种之一的 lowering。
  每个测试都要求"不依赖网络、不依赖宿主机已装第三方运行时"，所有夹具放在 `tests/fixtures/external_packages/` 下并随仓库提交。

10.文档：同步更新 `docs/USER_GUIDE.md`、`docs/USER_GUIDE_zh.md`、`docs/specs/ploy_language_spec*.md`、`docs/realization/` 下相关实现文档与 `README.md`。新增 `docs/realization/external_packages.md` / `docs/realization/external_packages_zh.md` 中英双份，详细说明各语言的查找顺序、CLI 选项、ABI 桥接细节与故障诊断。

11.约束：
  - 不允许最小实现/占位；
  - C++ 代码注释一律英文；
  - 全程保持与现有项目风格一致；
  - 现有的所有单元/集成/基准测试必须仍然通过；
  - 完成后在本条目末尾追加 `--end -done` 完成标记。

--end -done

2026-04-26-04

为什么runtime中Python/C++/Rust 是轻量包装，而不是完整实现呢？请帮我修改成完整的实现。

--end -done

2026-04-27-1

背景：经审查，`2026-04-26-03` 仅为 C++/Python/Java/.NET/Rust 五种语言接通了"外部包真实导入解析+链接"链路；`2026-04-26-01` 引入的 Go/JavaScript/Ruby 三个前端目前只完成了词法/语法骨架——`import "fmt"` / `import x from 'y'` / `require 'foo'` 仅停留在 AST 层，sema 没有解析包符号，lowering 没有发射桥接，CLI 中的 `--go-project` / `--node-modules` / `--gem-path` 等占位字段从未传递到 `FrontendOptions`，`runtime/src/libs/` 也没有 `__ploy_go_*` / `__ploy_js_*` / `__ploy_ruby_*` 桥。本次需求要求把这条链路按 `2026-04-26-03` 的同等深度对 Go/JavaScript/Ruby 补齐。

1.Go 前端：实现 `frontends/go/include/go_import_resolver.h` 与配套源码，解析当前项目根 `go.mod`（module 名 + 依赖 require/replace），并在以下位置按顺序查找包导入路径：
   - 项目本地 `<go_project_dir>/<import-path>` 子目录（package main 的内部包）；
   - `GOROOT/src/<import-path>`（标准库，自动从 `go env GOROOT` 探测）；
   - `GOPATH/pkg/mod/<escaped-path>@<version>/`（用户模块缓存）；
   - `--go-mod-cache=<dir>` 指定的额外缓存。
   解析到包目录后，扫描其中的 `.go` 文件用本前端自身的 `GoParser` 提取 `FuncDecl` / `TypeSpec` 作为外部符号，写入 `SemaContext`。Lowering 阶段对所有跨包调用生成 `__ploy_go_call`/`__ploy_go_load_pkg` 运行时桥引用而非裸名字。

2.JavaScript 前端：实现 `frontends/javascript/include/javascript_import_resolver.h`，按 Node.js 标准解析算法定位模块：
   - 相对路径（`./x`、`../y`）→ 加 `.js/.mjs/.cjs/.ts/.d.ts`、再尝试 `index.*`；
   - 包说明符（`lodash`）→ 沿 `<file_dir>/node_modules/<name>` 向上回溯，直到根目录；同样消费 `--node-modules=<dir>` 给出的额外根；
   - 命中后读取 `package.json`（解析 `main` / `module` / `types` / `exports`）；
   - 优先加载同目录的 `.d.ts` / `index.d.ts`（用现有 JS lexer 提取 `export` 声明的类型签名），无则回落到对应 `.js` 用 JS parser 提取顶层 `function`/`class`/`const` 声明；
   - 同步处理 CommonJS：`require('x')` 调用要被 sema 提升为隐式 `ImportDecl`。
   Sema 把解析出的符号写入 `SemaContext` 并标记为 `external`；lowering 为相应调用发射 `__ploy_js_call`/`__ploy_js_require`。

3.Ruby 前端：在 `ruby_ast.h` 增加 `RequireStmt`（区分 `require`/`require_relative`/`load`/`autoload`），parser 在表达式语句层把这些方法名提升为该 AST 节点；新增 `frontends/ruby/include/ruby_import_resolver.h`，按以下顺序查找：
   - `require_relative` → 相对当前文件目录；
   - `RUBYLIB` 环境变量；
   - `--gem-path=<dir>` 与项目 `Gemfile` 的 bundler 路径（复用 ploy 已有 `IndexRubyViaBundler`）；
   - `$LOAD_PATH` 默认 site-ruby 与 vendor-ruby（自动从 `ruby -e "puts $LOAD_PATH"` 探测）。
   解析到 `.rb` 后用 RbParser 提取 `MethodDecl` / `ClassDecl` / `ModuleDecl` 写入 SemaContext；lowering 为外部 `Module::method` 调用发射 `__ploy_ruby_call`/`__ploy_ruby_require`。

4.FrontendOptions 扩展：在 `frontends/common/include/language_frontend.h` 的 `FrontendOptions` 增加 `js_project_dir / node_modules_paths / ruby_project_dir / gem_paths / go_project_dir / go_module_paths` 六个字段，并由 `tools/polyc/src/stage_frontend.cpp` 从已有 `Settings` 字段透传，三个语言前端的 `Lower()` 真正消费这些选项。

5.polyc CLI：`driver.cpp --help` 中新增/归类显示 `--go-project=<dir>` / `--go-mod-cache=<dir>` / `--js-project=<dir>` / `--node-modules=<dir>` / `--ruby-project=<dir>` / `--gem-path=<dir>`；解析逻辑与 `Settings` 字段同步。

6.运行时桥接：在 `runtime/src/libs/` 增加 `go_rt.c`、`js_rt.c`、`ruby_rt.c`，分别用 `dlopen`/`LoadLibrary` + `dlsym`/`GetProcAddress` 加载并调用 lowering 发射的外部符号；缺失对应运行时时给出明确诊断（不崩溃）。`runtime/CMakeLists.txt` 同步加入新源文件并产出 `runtime_go_rt`/`runtime_js_rt`/`runtime_ruby_rt` 三个静态库（与 python/java/dotnet 风格一致）。

7.集成测试：在 `tests/integration/external_packages/` 增加：
   - `go/import_local_pkg.go` — 通过 `--go-project` 消费同项目下 `mathpkg/` 子包；
   - `javascript/import_node_module.js` — 通过 `--node-modules` 消费 `tests/fixtures/external_packages/javascript/node_modules/fakelib/` 提供的 `.d.ts`；
   - `ruby/require_lib.rb` — 通过 `--gem-path` 消费 `tests/fixtures/external_packages/ruby/lib/strutil.rb`。
   每个测试断言：sema 通过、lowering IR 中含真实外部符号引用、运行时缺失时报告明确诊断而非崩溃。

8.文档：更新 `README.md` 与 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md` 的"外部包消费"章节，把表格扩展到 8 种语言；扩写 `docs/realization/external_packages.md` 与 `_zh.md`，新增 Go/JavaScript/Ruby 三节（查找顺序、CLI、ABI 桥接、故障诊断）。

9.约束：
   - 不允许最小实现/占位；
   - C++ 代码注释一律英文；
   - 全程保持与现有项目风格一致；
   - 现有所有单元/集成/基准测试必须仍然通过；
   - 完成后在本条目末尾追加 `--end -done`。

--end -done

2026-04-27-2

编译器gui要支持渲染Markdown格式的文档（可以使用外部库）

--end -done

2026-04-27-3

背景：经过对全项目代码的审查，polyc 当前对各前端的"语言版本（language standard）"基本是固定的：C++ 走单一默认 dialect（无 `-std=c++17/20/23`）、Python 走 `Py3` 通用解析路径、Java 没有 `--release/-source/-target`、.NET 没有 `LangVersion / TargetFramework`、Rust 没有 `--edition`、Go 没有 `go.mod` 中 `go 1.x` 行的消费、JavaScript 没有 ECMAScript 版本切换、Ruby 没有 1.9/2.x/3.x 语法切换。Ploy 语言本身也没有任何"指定/约束目标语言版本"的语法。本次需求要求把"各语言多版本编译与管理"这条能力做实，并在 ploy 中加入"可显式指定语言版本（默认自动推断）"的语法与语义。具体要求如下：

1.前端能力按版本切换：
   - C++ 前端：在 `frontends/cpp/include/cpp_options.h`（如不存在则新增）中加入 `CppDialect { Cpp98, Cpp03, Cpp11, Cpp14, Cpp17, Cpp20, Cpp23, Cpp26 }`，由 `parser/sema/lowering` 共享。词法层根据 dialect 启用/禁用关键字（如 `co_await`/`concept` 仅 C++20+，`if consteval` 仅 C++23+，`auto` 类型推断仅 C++11+）；parser/sema 控制特性可见性（lambda、`constexpr if`、`requires`、`<=>`、模板形参 `auto`）。
   - Python 前端：加入 `PythonVersion { Py2_7, Py3_6, Py3_8, Py3_10, Py3_11, Py3_12, Py3_13 }`。lexer 控制 `print` 关键字 vs 函数、`async/await` 自 3.5+、`match/case` 自 3.10+、PEP 695 类型参数自 3.12+；sema 控制 walrus `:=`(3.8+)、PEP 604 联合类型 `X | Y`(3.10+)、`type` 语句(3.12+)。
   - Java 前端：加入 `JavaRelease { Java8, Java11, Java17, Java21, Java23 }`。已有 java8/17/21/23 支持要按 release 限制语法：`var` 自 10+、`record/sealed` 自 17+、pattern matching for switch 自 21+、unnamed variable `_` 自 21+、primitive patterns 自 23+。
   - .NET/C# 前端：加入 `DotnetLangVersion { Cs7_3, Cs8, Cs9, Cs10, Cs11, Cs12 }` 与 `TargetFramework { Net6, Net7, Net8, Net9 }`。控制 `record`(9+)、`init`(9+)、`global using`(10+)、`raw string`(11+)、primary constructors(12+)、`required` 成员(11+) 等。
   - Rust 前端：加入 `RustEdition { E2015, E2018, E2021, E2024 }`。控制 `async/await`(2018+)、`dyn` 关键字、`raw_ref` 与 `let-else`(2021+)、`gen` 块(2024+)；`use` 路径解析的 edition 差异要落到 sema。
   - Go 前端：从项目根 `go.mod` 中读取 `go 1.X` 行得出 `GoVersion { Go1_18, Go1_20, Go1_21, Go1_22, Go1_23 }`。控制泛型(1.18+)、`any` 别名(1.18+)、`min/max/clear` 内建(1.21+)、`for range int`(1.22+)。
   - JavaScript 前端：加入 `EcmaVersion { Es5, Es2015, Es2017, Es2020, Es2022, Es2023, Esnext }`。控制 `class/let/const`(Es2015+)、`async/await`(Es2017+)、optional chaining/null coalescing(Es2020+)、top-level await/private fields(Es2022+)。
   - Ruby 前端：加入 `RubyVersion { Ruby1_9, Ruby2_7, Ruby3_0, Ruby3_2, Ruby3_3 }`。控制 `safe navigation &.`(2.3+)、pattern matching(3.0+)、`it` 块参数(3.4+ 预留)、endless method(3.0+)、shorthand hash literal(3.1+)。

2.每个前端的 `ILanguageFrontend::Lower()` / `Analyze()` 入口需通过 `FrontendOptions` 接收对应版本字段；`frontends/common/include/language_frontend.h` 的 `FrontendOptions` 增加 `cpp_dialect / python_version / java_release / dotnet_lang_version / dotnet_target_framework / rust_edition / go_version / ecma_version / ruby_version` 九个字段，默认 `Auto`（让前端自行推断）。各前端在自己的 `Lower()` 内若收到 `Auto`，按以下顺序推断：源文件首行/首注释的版本指示（如 `// @cpp-dialect: c++20`）→ 项目配置文件（`go.mod` / `Cargo.toml [package].edition` / `*.csproj <LangVersion>` / `package.json engines.node` / `pyproject.toml [tool.polyc].python` / `Gemfile .ruby-version`）→ 工具链可探测版本（`python --version` / `dotnet --version` / `rustc --edition` / `go env GOVERSION`）→ 该语言保守默认（C++20、Py3.11、Java17、Cs11/Net8、Rust 2021、Go1.21、Es2022、Ruby3.2）。

3.polyc CLI 扩展（在 `tools/polyc/src/driver.cpp` 与 `tools/polyc/include/compilation_pipeline.h` 的 `Settings` 中同步落地）：新增并在 `--help` 中归类显示
   - `--std=c++17|c++20|c++23|c++26`（同时接受 `-std=` 形式以兼容主流惯例）；
   - `--python-version=3.10|3.11|3.12|3.13`；
   - `--java-release=8|11|17|21|23`；
   - `--cs-lang=7.3|8|9|10|11|12`、`--target-framework=net6|net7|net8|net9`；
   - `--rust-edition=2015|2018|2021|2024`；
   - `--go-version=1.21|1.22|1.23`；
   - `--ecma=es2017|es2020|es2022|esnext`；
   - `--ruby-version=2.7|3.0|3.2|3.3`；
   每个选项缺省值均为 `auto`，并通过 `stage_frontend.cpp` 透传到对应前端。CLI 同时支持 `--list-language-versions` 子命令，打印当前 polyc 支持的全部语言/版本矩阵。

4.工具链管理（多版本共存）：新增 `tools/polyver/`（命令名 `polyver`，与 `polyc/polyld/polyasm/polyopt/polyrt/polytopo/polybench/polyui` 风格一致）作为"语言工具链管理器"。功能：
   - `polyver list <lang>`：列出已检测到的该语言所有可用工具链（路径、版本、是否默认）；
   - `polyver detect`：在 PATH、常见安装目录（Windows: `C:\Python*` / `C:\Program Files\Java\*` / `C:\Program Files\dotnet` / `%USERPROFILE%\.cargo`，macOS/Linux: `/usr/bin`、`/opt/homebrew`、`~/.pyenv`、`~/.rbenv`、`~/.nvm`、`~/.sdkman`、`/usr/lib/jvm`、`/usr/local/go`）下扫描并写入用户级 `~/.polyglot/toolchains.json`；
   - `polyver use <lang> <version>`：把指定版本设为当前工程默认（写入工程根 `.polyglot/toolchains.lock`）；
   - `polyver path <lang> <version>`：打印该工具链的可执行文件绝对路径；
   - polyc 在解析 `Auto` 时若发现工程根存在 `.polyglot/toolchains.lock`，优先使用其中固定的版本；其次读取 `~/.polyglot/toolchains.json`。
   `polyver` 必须有完整源码 + 单元测试，禁止仅是壳工具。

5.Ploy 语言新增"语言版本指定"语法：
   - 文件级：`LANG cpp = c++20; LANG python = 3.11; LANG rust = 2021;`，作用域为整个 `.ploy` 文件，必须出现在文件顶部（`IMPORT` 之前）。
   - 块级（覆盖文件级）：`WITH LANG(cpp = c++23) { ... }`，块内的 `LINK`/`CALL`/内联 `cpp::...` 引用按 c++23 行为解析。
   - 单符号级（覆盖块级）：`LINK cpp::math::add @LANG(c++23);`、`CALL python::np::array(x) @LANG(3.12);`，仅对该一处 LINK/CALL 生效。
   - 缺省（未写 `LANG` 也无 `@LANG`）：自动推断，规则同第 2 条。
   - 解析与 sema：
     - 在 `frontends/ploy/include/ploy_ast.h` 增加 `LangDirective`（文件级）、`WithLangBlock`（块级）、`LangAnnotation`（单符号级）三个 AST 节点；
     - parser 在 `parser.cpp` 中识别 `LANG` 与 `WITH LANG(...)` 与 `@LANG(...)` 三种形式，关键字 `LANG` 加入 `ploy_lexer.cpp` 关键字表；
     - sema 在 `sema.cpp` 中维护"语言版本作用域栈"，对每个跨语言引用解析时，把当前栈顶版本一并写入 `CallDescriptor` 与 `LinkEntry`，并把版本信息透传到对应前端的 lowering（同一个 `.ploy` 文件内允许同语言多版本共存，各 LINK/CALL 走自己的版本签名）。
   - lowering：将版本信息写入 aux 描述符（`*_link_descriptors.paux`）的二进制结构里，供 `polyld` 在生成跨语言桥时按版本选择正确的 ABI 桥代码。

6.SemaContext 与诊断：每个前端在加载外部包符号、解析关键字、生成 IR 时，遇到"当前 dialect/version 不支持的语法"时，必须以 `DiagError` 报告——给出版本不匹配的明确信息（如 `'co_await' requires C++20 or later (current: c++17)`），定位到出现该语法的源位置；不允许静默降级。`docs/realization/diagnostics.md` 中新增 `E_LANG_VERSION_MISMATCH`、`W_LANG_VERSION_FALLBACK`、`E_TOOLCHAIN_NOT_FOUND` 三个错误码。

7.运行时与链接：
   - `runtime/src/libs/python_rt.c` / `java_rt.c` / `dotnet_rt.c` / `go_rt.c` / `js_rt.c` / `ruby_rt.c` 分别接受"目标版本"参数，运行时从 `.polyglot/toolchains.lock` 选择对应解释器/JVM/CoreCLR/Node/MRI 的实际可执行路径，确保 `polyc` 编译产物在多版本共存机器上选择正确的运行时；
   - `polyld` 在解析跨语言桥时按 `CallDescriptor.lang_version` 字段选择对应的 ABI 适配（例如 Python 3.10 vs 3.12 的 `Py_buffer` / `PyType_Spec` 差异、Java 8 vs Java 21 的 `MethodHandle` 调用约定差异）。

8.集成测试：在 `tests/integration/language_versions/` 下增加：
   - `cpp/cpp17_vs_cpp20_concept.cpp` + 对应 ploy 触发 `requires` 关键字仅在 `--std=c++20` 通过；
   - `python/walrus_in_3_8.py` + ploy 用 `LANG python = 3.6;` 期望诊断；
   - `java/record_in_17.java` + ploy 用 `LANG java = 8;` 期望诊断；
   - `dotnet/raw_string_in_cs11.cs`；
   - `rust/let_else_in_2021.rs`；
   - `go/generic_in_1_18.go`；
   - `js/optional_chaining_in_es2020.js`；
   - `ruby/pattern_match_in_3_0.rb`；
   - `ploy/per_callsite_version.ploy`：同一文件 `LINK cpp::a @LANG(c++17);` 与 `LINK cpp::b @LANG(c++23);` 共存，验证两套 ABI 桥同时生成。
   每个测试都覆盖"通过路径"+"版本不匹配诊断路径"两种断言，禁止 `REQUIRE(true)`。

9.UI 集成：`polyui` 的 Settings 页面新增"Toolchains"标签页，调用 `polyver list/detect`，让用户在 GUI 中选择各语言默认版本；编辑器对 `.ploy` 中的 `LANG` / `WITH LANG` / `@LANG` 语法提供高亮、补全、悬停提示；拓扑面板节点 hover-tip 中显示该节点解析时使用的具体语言版本。

10.文档：
   - 新增 `docs/realization/language_versions.md` 与 `docs/realization/language_versions_zh.md`，详细说明各语言支持的版本矩阵、`polyver` 用法、`.polyglot/toolchains.{json,lock}` 格式、ploy 中三种作用域语法、自动推断算法、版本不匹配诊断与故障排查；
   - 更新 `docs/specs/ploy_language_spec.md` 与 `_zh.md`：新增 `LANG` / `WITH LANG` / `@LANG` 三条语法的 EBNF、语义解释与示例；
   - 更新 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md` 的"编译命令"与"多语言混合"章节；
   - 更新 `README.md` 的"Supported Languages"表格，把每行扩展为"language : versions"；
   - 同步刷新 `docs/api/api_reference.md` / `_zh.md` 中前端 API 与 `FrontendOptions` 字段说明。

11.约束：
   - 不允许最小实现/占位；
   - C++ 代码注释一律英文；
   - 全程保持与现有项目风格一致；
   - 现有所有单元/集成/基准测试必须仍然通过；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因引入 ploy 新语法、CLI 新选项与新工具 `polyver`，版本号建议从 `1.0.0` 升级为 `1.1.0`（次版本号 +1，向后兼容）。

--end -done

2026-04-27-4

背景：当前 `polyui` 的设置仅通过 `SettingsDialog` 的 GUI 表单写入 `QSettings`（注册表/平台原生存储），既不便于跨机器同步、无法纳入 Git 版本控制、也无法在没有 GUI 的环境（CI、远程开发、SSH）中调整；不同模块（编辑器、构建、调试、Git、插件、拓扑、Toolchains 等）的设置散落在 `QSettings` 的不同 key 中，没有统一 schema 与默认值文档。本次需求要求引入"VS Code 风格"的设置体系：所有设置以 JSON 文件为唯一事实源，GUI 只是 JSON 的可视化编辑器；同时支持用户级与工程级设置，并提供命令面板/快捷键直接打开 JSON 进行编辑。具体要求如下：

1.设置存储分层（与 VS Code 完全对齐）：
   - **默认设置（Default Settings）**：内置在 polyui 二进制资源中（`tools/ui/common/resources/default_settings.json`），只读，用于回退；GUI 中以只读视图展示，便于用户对照。
   - **用户设置（User Settings）**：`%APPDATA%/PolyglotCompiler/settings.json`（Windows）/ `~/Library/Application Support/PolyglotCompiler/settings.json`（macOS）/ `~/.config/PolyglotCompiler/settings.json`（Linux），跨工程生效。
   - **工程设置（Workspace Settings）**：当前工程根的 `.polyglot/settings.json`，仅对当前工程生效，优先级高于用户设置；与 `.polyglot/toolchains.lock`（见 2026-04-27-3）同目录。
   - **覆盖优先级**：默认 < 用户 < 工程；运行时合并为单个生效设置树（effective settings），任意层级的字段缺失时按上级回退。

2.JSON Schema 与字段命名（点号分组，与 VS Code 命名风格一致）：
   - 在 `tools/ui/common/resources/settings_schema.json` 中提供完整 JSON Schema（draft-2020-12），覆盖以下命名空间：
     - `editor.*`（fontFamily / fontSize / tabSize / insertSpaces / wordWrap / minimap.enabled / lineNumbers / renderWhitespace / formatOnSave / autoSave / autoSaveDelay / rulers）
     - `workbench.*`（colorTheme / iconTheme / startupEditor / showToolbar / showStatusBar / showExplorer / sideBar.location）
     - `terminal.*`（fontFamily / fontSize / shell.windows / shell.linux / shell.osx / cursorStyle）
     - `build.*`（cmakePath / generator / parallelJobs / configurations / defaultConfiguration）
     - `debug.*`（gdbPath / lldbPath / breakOnException / showInlineValues / consoleEncoding）
     - `git.*`（path / autoFetch / autoFetchPeriod / confirmSync / defaultBranch）
     - `plugins.*`（enabled / loadOrder / sandboxTimeoutMs / sandboxMemoryMb）
     - `topology.*`（layoutAlgorithm / showPortLabels / animateTransitions / drillDown.openInNewWindow）
     - `polyc.*`（path / extraArgs / progressFormat / cacheEnabled）
     - `toolchains.*`（cpp.default / python.default / java.default / dotnet.default / rust.default / go.default / js.default / ruby.default）（与 2026-04-27-3 联动）
     - `ploy.*`（strictMode / packageDiscoveryEnabled / langDirectiveAutoInfer）
     - `keybindings.*`（命令名 → 快捷键字符串，与 VS Code `keybindings.json` 一致风格）
     - `files.*`（associations / exclude / encoding / eol）
   - Schema 必须给出每个字段的 `type`、`default`、`description`（中英双语，至少 `description` 与 `descriptionZh` 两字段）、`enum`（如适用）、`pattern`（如适用）。

3.运行时设置服务（C++/Qt 实现，所有 UI 模块统一调用）：
   - 新增 `tools/ui/common/include/settings_service.h` + `settings_service.cpp`，提供 `SettingsService`：
     - `void Load()`：读取默认/用户/工程三层 JSON，合并为 effective settings，使用 `nlohmann::json` 库（项目已通过 `Dependencies.cmake` 引入 fmt，可同样规范引入 nlohmann/json，禁止自己手写 JSON 解析）。
     - `template<typename T> T Get(QStringView key, T fallback) const;` 与 `void Set(QStringView key, const QJsonValue &value, Scope scope);`（`Scope = User | Workspace`）。
     - `QObject` 信号 `settingsChanged(QString key, QJsonValue oldValue, QJsonValue newValue)`，所有面板订阅并热更新（不要求重启）。
     - `Watch()`：用 `QFileSystemWatcher` 监听用户/工程 JSON 文件变更，文件外部修改后 200ms 防抖重新加载并发出信号。
     - `ValidateAgainstSchema()`：基于 JSON Schema 校验，错误以 `Diagnostics` 形式上抛到 UI 状态栏与 Output 面板（行/列定位）。
   - 全项目所有 `QSettings` 访问点（`mainwindow.cpp` / `settings_dialog.cpp` / `build_panel.cpp` / `debug_panel.cpp` / `git_panel.cpp` / `terminal_panel.cpp` / `topology_panel.cpp` / `editor/code_editor.cpp` / `plugin_manager.cpp`）改为通过 `SettingsService` 访问，禁止保留 `QSettings` 旁路写入；保留一次性的"旧 QSettings → user settings.json"迁移工具（`MigrateLegacyQSettings()`），首次启动时自动执行并备份原数据到 `settings.json.qsettings.bak`。

4.设置编辑器 UI（VS Code 风格）：
   - 重写 `SettingsDialog`，命令面板/菜单/快捷键 `Ctrl+,` 打开 `SettingsPage`（停靠式或独立窗口），布局分两栏：
     - 左侧：搜索框 + 命名空间树（Editor / Workbench / Terminal / Build / Debug / Git / Plugins / Topology / Polyc / Toolchains / Ploy / Keybindings / Files）。
     - 右侧：当前命名空间下所有字段的可视化编辑器，根据 schema 类型渲染 `QLineEdit / QSpinBox / QDoubleSpinBox / QCheckBox / QComboBox / QFontComboBox / QKeySequenceEdit / QListWidget`；每项右上角显示"已被工程覆盖"等来源标签（与 VS Code 一致）。
     - 顶部按钮：`Open User settings.json` / `Open Workspace settings.json` / `Reset to Default`（针对单字段或全部）；右上角"⋯"菜单含 `Switch to JSON view`（直接切到内置 JSON 编辑器并定位到该字段行）。
   - 新增内置 `SettingsJsonEditor`（基于现有 `CodeEditor` + 新增 JSON 高亮器 + JSON Schema 实时校验），打开 `settings.json` / `.polyglot/settings.json` 时自动启用补全（基于 schema）、悬停文档提示、波浪线诊断、保存时自动校验。
   - 命令面板（新增 `CommandPalette`，`Ctrl+Shift+P`）必须包含至少以下命令：
     - `Preferences: Open Settings (UI)`
     - `Preferences: Open User Settings (JSON)`
     - `Preferences: Open Workspace Settings (JSON)`
     - `Preferences: Open Default Settings (JSON, read-only)`
     - `Preferences: Open Keyboard Shortcuts`
     - `Preferences: Open Keyboard Shortcuts (JSON)`
     - `Preferences: Reset Setting...`（带搜索）

5.快捷键体系（与 VS Code 风格一致）：
   - 用户级 `keybindings.json`（与 `settings.json` 同目录，独立文件）：数组形式 `[{"key": "ctrl+s", "command": "workbench.action.files.save", "when": "editorTextFocus"}]`。
   - `KeybindingService`（`tools/ui/common/include/keybinding_service.h`）：注册命令、解析按键序列（含 chord，如 `ctrl+k ctrl+s`）、`when` 表达式求值（最少支持 `editorTextFocus` / `terminalFocus` / `debugRunning` / `!suggestWidgetVisible`）。
   - GUI 中 `Ctrl+K Ctrl+S` 打开"Keyboard Shortcuts"列表页，支持搜索/筛选（按命令名、按键、来源），点击单项可改键并写回 `keybindings.json`。

6.工程模板与 `.polyglot/` 目录契约：
   - 当用户首次在工程中点击"Save as Workspace Settings"或在 Explorer 右键"Configure Workspace Settings"时，自动创建 `.polyglot/` 目录与 `settings.json`（含一份注释模板），并把 `.polyglot/` 加入 `.gitignore` 的反向白名单建议（`!.polyglot/settings.json` / `!.polyglot/toolchains.lock`，提示用户是否纳入版本控制）。
   - 所有"模板创建"（2026-04-09-16 的模板中心）生成的工程，根目录默认含 `.polyglot/settings.json` 与最小化默认配置。

7.CLI 一致性：
   - `polyc` / `polyld` / `polybench` / `polyrt` / `polytopo` / `polyver` 在启动时也读取同一份 `.polyglot/settings.json` 中的相关命名空间字段（`polyc.*` / `toolchains.*` / `ploy.*`），命令行参数仍可显式覆盖。CLI 工具新增 `--settings <path>` 显式指定 JSON 文件、`--print-effective-settings` 打印合并后的最终设置树（用于排错）。
   - 在 `tools/common/`（新增）放置 `effective_settings_loader.{h,cpp}`，CLI 与 UI 共用同一加载/合并/校验代码，禁止出现两份实现。

8.热更新与一致性：
   - GUI 中改动设置 → 立即写入对应 scope 的 JSON → 触发 `settingsChanged` → 所有订阅面板即时刷新（字体、主题、编辑器规则、终端 shell、构建并行度、调试器路径、Git 路径等）。
   - 反向：用户在外部编辑器修改 JSON → `QFileSystemWatcher` 触发重新加载 → GUI 即时刷新；若 schema 校验失败，弹出诊断条 + Output 面板红色行，并保留旧值不应用。

9.集成测试与单元测试：
   - 新增 `tests/unit/tools/ui/settings_service_test.cpp`：覆盖默认/用户/工程三层合并、`Get/Set` 类型转换、信号触发、schema 校验失败路径、迁移 `QSettings` 路径。
   - 新增 `tests/unit/tools/ui/keybinding_service_test.cpp`：覆盖 chord 解析、`when` 表达式求值、冲突检测、`keybindings.json` 读写。
   - 新增 `tests/integration/ui/settings_e2e_test.cpp`（QTest）：在 headless 模式下打开 SettingsPage、修改字段、断言 JSON 文件内容与 GUI 显示同步、外部修改 JSON 后断言 GUI 刷新。
   - 现有测试不得回归；CI 在 ASan 下运行新增测试。

10.文档：
   - 新增 `docs/realization/settings_system.md` 与 `docs/realization/settings_system_zh.md`，详细说明三层合并语义、JSON Schema 字段表、命令面板命令清单、快捷键体系、`.polyglot/` 目录契约、迁移流程、常见故障排查。
   - 更新 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md`：新增"设置（Settings）"章节，给出 `settings.json` 示例与命令面板截图位说明。
   - 更新 `README.md`：在 Features 表中加入 `JSON-first settings (VS Code style) with UI editor and command palette`。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：新增 `SettingsService` / `KeybindingService` / `CommandPalette` 公共 API。

11.约束：
   - 不允许最小实现/占位（如"仅打开 JSON 不做合并"等不接受）；
   - C++ 代码注释一律英文；
   - 全程保持与现有项目风格一致（命名空间 `polyglot::ui`，文件命名小写下划线）；
   - JSON 解析统一使用 `nlohmann/json`，禁止自写解析器；
   - 现有所有单元/集成/基准测试必须仍然通过；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因引入 JSON 设置体系、命令面板、快捷键体系，建议版本号 `1.1.0 → 1.2.0`（次版本号 +1，向后兼容；保留 QSettings 自动迁移）。

--end -done

2026-04-27-5

背景：当前 `polyui` 的主题仅在 `SettingsDialog` 中以一组硬编码 `setStyleSheet(QString::fromLatin1("..."))` 字符串切换（Light / Dark / Solarized 等若干内置主题），主题样式与 C++ 代码强耦合，用户无法自定义颜色、无法分发主题包、也无法独立编辑/调试样式。本次需求要求把主题体系做成"VS Code 风格"的可外部加载、可组合、可分发的主题系统：用户既可通过外部 CSS/QSS 文件覆盖控件样式，也可通过 JSON 主题描述文件定义编辑器与 UI 的语义化颜色（token colors / workbench colors），并提供独立的"主题管理页面（Theme Manager）"用于浏览、安装、启用、预览、导出主题。具体要求如下：

1.主题文件格式与目录契约：
   - **JSON 主题（语义化颜色，主形式）**：扩展名 `.polytheme.json`，schema 在 `tools/ui/common/resources/theme_schema.json` 提供（draft-2020-12）。结构与 VS Code Color Theme 对齐：
     ```json
     {
       "name": "Polyglot Dark+",
       "id": "polyglot.dark-plus",
       "type": "dark",                 // dark | light | high-contrast
       "version": "1.0.0",
       "author": "...",
       "extends": "polyglot.dark",     // optional, inherit and override
       "colors": {                      // workbench colors (UI elements)
         "editor.background": "#1e1e1e",
         "editor.foreground": "#d4d4d4",
         "editorLineNumber.foreground": "#858585",
         "sideBar.background": "#252526",
         "statusBar.background": "#007acc",
         "panel.border": "#80808059",
         "tab.activeBackground": "#1e1e1e",
         "topology.node.background": "#2d2d30",
         "topology.edge.valid": "#4ec9b0",
         "topology.edge.incompatible": "#f48771"
       },
       "tokenColors": [                 // editor syntax tokens
         { "scope": ["keyword.control.ploy"], "settings": { "foreground": "#c586c0", "fontStyle": "bold" } },
         { "scope": ["string.quoted"],        "settings": { "foreground": "#ce9178" } }
       ],
       "qss": "extra.qss"               // optional sibling QSS file for finer control
     }
     ```
   - **CSS/QSS 主题（控件样式覆盖，辅助形式）**：扩展名 `.qss`（Qt Style Sheets，是 CSS 子集 + Qt 扩展）。允许独立存在（仅 QSS 主题，无 JSON 语义颜色），也可作为 JSON 主题的 `qss` 字段被联动加载；加载时 `SettingsService` 会先把 JSON 中的 `colors.*` 注入为 QSS 变量（形式 `--editor-background` → 编译期替换或 Qt 6 的 `QPalette::Color` 映射），便于 QSS 引用。
   - **目录契约（与 settings 对齐）**：
     - 内置主题：`tools/ui/common/resources/themes/{polyglot-light,polyglot-dark,polyglot-high-contrast}.polytheme.json`（随二进制打包，只读）。
     - 用户主题：`<userConfigDir>/PolyglotCompiler/themes/*.polytheme.json` 与同名 `*.qss`。
     - 工程主题：`.polyglot/themes/*.polytheme.json`，作用域仅当前工程。
     - 加载优先级：内置 < 用户 < 工程。
   - **主题包（可分发）**：扩展名 `.polythemepack`（实质是 zip），包含一个 `pack.json`（描述包名、作者、license、依赖的最小 polyui 版本）+ 一个或多个 `.polytheme.json` + 资源（图标、字体、QSS 片段）。主题包可被插件系统识别并安装。

2.主题运行时服务（C++/Qt）：
   - 新增 `tools/ui/common/include/theme_service.h` + `theme_service.cpp`，提供 `ThemeService`：
     - `void Scan()`：扫描三层目录 + 已安装主题包，构建 `available_themes_` 列表。
     - `bool Activate(const QString &theme_id)`：解析 JSON（含 `extends` 链）→ 合成最终 `colors`/`tokenColors` 表 → 应用到全局 `QPalette` + 加载/合成 QSS → 通过现有 `SyntaxHighlighter` 重新着色所有打开编辑器 → 通知 `TopologyPanel` 重绘节点/边色。
     - `Reload()`：用 `QFileSystemWatcher` 监听激活主题文件变更，500ms 防抖热重载（无需重启 polyui，便于主题作者实时调试）。
     - `ExportToFile(const QString &theme_id, const QString &out_path)`：将合成后（含 extends 展开）的主题导出为单一 `.polytheme.json`。
     - `Install(const QString &pack_path)` / `Uninstall(const QString &theme_id)`：安装/卸载 `.polythemepack` 到用户主题目录。
     - `ValidateAgainstSchema()`：基于 `theme_schema.json` 校验，错误以诊断形式上抛。
   - 现有所有 `setStyleSheet` 硬编码（`mainwindow.cpp` / `settings_dialog.cpp` / `git_panel.cpp` / `build_panel.cpp` / `debug_panel.cpp` / `topology_panel.cpp` 等）必须移除并改为通过 `ThemeService` 注入；不允许任何模块再写裸 `setStyleSheet` 字面量（除非读取自主题文件）。
   - 与 `SettingsService`（2026-04-27-4）联动：`workbench.colorTheme`（字符串，主题 id）和 `workbench.iconTheme` 字段是激活主题的唯一入口；用户在 SettingsPage 改 `workbench.colorTheme` 即触发 `ThemeService::Activate`。

3.主题管理页面（Theme Manager UI，独立功能页）：
   - 入口：菜单 `View → Theme Manager`、命令面板 `Preferences: Color Theme` 与 `Preferences: Manage Themes`、快捷键 `Ctrl+K Ctrl+T`（与 VS Code 一致）。
   - 布局（独立窗口或停靠面板，三栏）：
     - 左栏：主题列表（按来源分组：Built-in / User / Workspace / Installed Packs），支持搜索、按类型筛选（Dark/Light/HC）、置顶收藏。
     - 中栏：所选主题的实时预览区（一个固定的"展示用 mini-IDE"：含一个代码编辑器片段、一个拓扑图小片段、一个终端片段、一个状态栏片段），鼠标悬停可显示该色键名。
     - 右栏：主题元信息（name / id / type / version / author / extends 链），下方是"Color Tokens"折叠树（按 `editor / sideBar / statusBar / topology / terminal / panel / button` 等分组），每项显示色块与 hex 值，可点击进入"Override"模式（写入用户级覆盖到 `<userConfigDir>/themes/<id>.override.polytheme.json`，不污染原主题文件）。
   - 顶部工具栏按钮：
     - `Activate`：激活当前选中主题（写入 `workbench.colorTheme` 设置）。
     - `Edit`：在内置 JSON 编辑器中打开该主题文件（带 schema 补全/校验/波浪线，复用 2026-04-27-4 的 `SettingsJsonEditor`）。
     - `Edit QSS`：若主题含/可关联 `.qss`，在 QSS 编辑器中打开（复用 `CodeEditor` + 新增 QSS 高亮器）。
     - `Duplicate`：基于当前主题创建一份副本到用户目录，自动以 `extends` 引用原主题，仅写入差异。
     - `Export`：导出合成后的单文件主题。
     - `Install from File`：从本地选择 `.polythemepack` 安装；`Install from URL`：从 URL 下载并安装（带 SHA-256 校验提示）。
     - `Uninstall`：卸载用户/包主题（内置主题不可卸载，按钮禁用）。
   - 实时预览：编辑主题文件保存（或在右栏改 token 颜色）后，预览区与全局 UI 同步刷新（依赖 `ThemeService::Reload`），无需重启。

4.主题作者工作流支持：
   - 命令 `Developer: Generate Color Theme From Current Settings`：把当前生效配色（含用户已做的所有 override）导出为一份新的 `.polytheme.json` 模板。
   - 命令 `Developer: Inspect Editor Token`：在编辑器中点击任意 token，弹出该 token 的 scope 链与当前命中的 `tokenColors` 规则（与 VS Code 同名命令一致），便于精确编写规则。
   - 命令 `Developer: Inspect UI Color Key`：鼠标悬停任意 UI 元素，弹出对应的 workbench color key（如 `sideBar.background`）。
   - 这三条命令均挂入命令面板（2026-04-27-4 引入）。

5.内置主题（首批必须随发布提供，作为后续主题的参考实现）：
   - `polyglot-light`：浅色，作为默认。
   - `polyglot-dark`：深色。
   - `polyglot-high-contrast`：高对比度（黑底，符合无障碍）。
   - `polyglot-solarized-light` / `polyglot-solarized-dark`：经典 Solarized。
   - 全部以纯 JSON 形式提供（不允许仍有硬编码 QSS 字面量），并与文档中的"色键参考表"严格一一对应。

6.CLI 一致性（与 2026-04-27-4 共享加载层）：
   - `polyui --theme <id|path>` 启动时强制以指定主题运行（用于截图、文档生成、CI 视觉回归）。
   - 新增 `polyui --list-themes` 仅列出可用主题与来源后退出。
   - `polyui --validate-theme <path>` 仅做 schema 校验后退出，错误以非零退出码 + JSON 诊断输出（便于主题作者在 CI 中校验）。

7.测试：
   - 新增 `tests/unit/tools/ui/theme_service_test.cpp`：覆盖 schema 校验、`extends` 链合成、三层目录优先级、热重载信号、`Install/Uninstall` 路径、QSS 联动注入。
   - 新增 `tests/integration/ui/theme_manager_e2e_test.cpp`（QTest，headless）：打开 Theme Manager → 选中 dark → Activate → 断言 `workbench.colorTheme` 写入；外部修改 JSON → 断言预览与全局刷新；安装 `.polythemepack` → 断言出现在列表；导出 → 断言文件存在且可被再次解析。
   - 新增视觉回归冒烟：`polyui --theme polyglot-dark --headless --screenshot <out.png>` 在 CI 中产出截图并与基线对比（容差像素 < 阈值）。
   - 不得使用 `REQUIRE(true)`；现有测试不得回归。

8.文档：
   - 新增 `docs/realization/theme_system.md` 与 `docs/realization/theme_system_zh.md`，包含：
     - 三层目录加载顺序与优先级；
     - JSON Schema 全字段参考表（含每个 workbench color key 的中英双语含义与作用控件清单）；
     - `tokenColors` scope 命名约定（与各前端 lexer 输出的 scope 对齐：`keyword.control.ploy` / `entity.name.function.cpp` 等，必须列全）；
     - QSS 与 JSON colors 的联动机制；
     - 主题包 `.polythemepack` 打包格式；
     - 主题作者完整工作流（Inspect → Edit → Reload → Export → Pack）；
     - 故障排查（常见加载失败、热重载未触发、色键拼写错误）。
   - 更新 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md`：新增"主题（Themes）"章节。
   - 更新 `README.md`：在 Features 表中加入 `External JSON/QSS theme system with theme manager`。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：新增 `ThemeService` 与 Theme Manager 公共 API。

9.约束：
   - 不允许最小实现/占位（如"仅切换 stylesheet 字符串而不接 JSON schema"等不接受）；
   - C++ 代码注释一律英文；
   - JSON 解析统一使用 `nlohmann/json`，QSS 解析复用 Qt 自带能力；
   - 所有内置主题必须以 JSON 文件存在，不允许保留硬编码 stylesheet 分支；
   - 全程保持与现有项目风格一致（命名空间 `polyglot::ui`，文件命名小写下划线）；
   - 现有所有单元/集成/基准测试必须仍然通过；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 与 2026-04-27-4 同属设置/外观体系扩展，建议在同一发布周期内合并发布；版本号在 2026-04-27-4 的基础上由 `1.2.0 → 1.3.0`（次版本号 +1，向后兼容）。

--end -done

2026-04-28-1

背景：`frontends/common/src/token_pool.cpp` 目前是占位文件（实现部分只有一行注释 `// TokenPool is header-only for now.`），而 `frontends/common/include/token_pool.h` 中的 `TokenPool` 也只是对 `std::vector<Token>` 的极薄包装：没有 lexeme 的 arena/intern 机制、没有标识符去重、没有 snapshot/restore、没有线程安全、没有内存统计；全仓 grep 显示除 `tests/unit/frontend/preprocessor_tests.cpp` 之外没有任何生产代码使用它，9 个语言前端的 lexer/parser 仍各自管理 `std::vector<Token>`，与 `docs/specs/namespace_architecture.md` 第 131 行将 `TokenPool` 列为 `polyglot::frontends` 公共基础设施的承诺严重不符。本次需求要求把 `TokenPool` 做成真正的、被所有前端共享的 token 基础设施，并把 `token_pool.cpp` 从占位升级为完整实现。具体要求如下：

1.数据模型与 API：
   - 在 `frontends/common/include/token_pool.h` 中引入：
     - `class StringArena`：单调增长的字节 arena（默认 64 KiB chunk，可配置），提供 `std::string_view Intern(std::string_view s)`，所有 lexeme 与标识符文本统一在此分配；arena 析构时一次性释放，禁止逐 token 释放。
     - `using StringRef = std::string_view`：所有 `Token::lexeme` 字段类型从 `std::string` 改为 `StringRef`，指向 `StringArena` 中的稳定地址（`TokenPool` 生命周期内永不失效）。
     - `using TokenHandle = uint32_t`：稳定的 token 句柄；`AST` 与 `Diagnostics` 改为持有 `TokenHandle` 而非 `Token` 引用，避免 vector 重分配后悬挂。
     - `class IdentifierTable`：基于开放寻址的标识符去重表，`SymbolId Intern(StringRef name)` 返回紧凑 32 位 id；用于 keyword 识别、scope 查找、跨 token 比较的零拷贝快速路径。
     - `class TokenPool` 提供：
       - `TokenHandle Add(Token token)`、`const Token &Get(TokenHandle h) const`、`size_t Size() const`、`Span<const Token> All() const`；
       - `StringRef InternLexeme(std::string_view raw)`、`SymbolId InternIdentifier(std::string_view raw)`；
       - `struct Snapshot { size_t token_count; size_t arena_offset; size_t identifier_count; };`
       - `Snapshot Save() const` / `void Restore(const Snapshot &)`：用于 parser 的推测性解析（lookahead 失败回滚），arena 与 identifier table 必须随之回退（要求 arena 支持 `RewindTo(offset)`，identifier table 支持高水位回退）；
       - `struct PoolStats { size_t tokens; size_t arena_bytes; size_t arena_capacity; size_t unique_identifiers; size_t intern_hits; size_t intern_misses; }`；`PoolStats Stats() const`；
       - `void Reset()`：清空全部 token / arena / identifier table，复用已分配的 chunk（不归还 OS），用于编译多文件时的池复用。
   - **不允许** 仍以 `std::vector<Token>` 为 `TokenPool` 的唯一存储；必须使用 `std::deque<Token>` 或 chunked storage，保证插入不让既有 `Token&` 失效（`TokenHandle` 永远稳定，`Get` 返回的引用在 `Reset/Restore` 之前永远稳定）。
   - 所有公共类型必须放在 `polyglot::frontends` 命名空间，文件名 `token_pool.h` / `token_pool.cpp` / `string_arena.h` / `string_arena.cpp` / `identifier_table.h` / `identifier_table.cpp`，注释一律英文。

2.线程安全模型：
   - 默认 `TokenPool` 为单线程使用（lexer/parser 拥有专属池），不引入锁开销。
   - 新增 `class SharedTokenPool`：在 `TokenPool` 之上叠加 `std::shared_mutex`，`Add` / `InternLexeme` / `InternIdentifier` 取写锁，`Get` / `All` / `Stats` 取读锁；用于 polyui 后台多文件并行索引、polyc 在 `--jobs N` 下的并行词法化。
   - `string_view` 必须保证跨线程读安全：arena 一旦分配某段就只追加、不移动（chunk 链表结构），保证既有 `string_view` 永不失效。

3.占位文件升级（核心交付物）：
   - `frontends/common/src/token_pool.cpp` 必须给出完整实现，至少包含：
     - `StringArena::Intern` / `StringArena::RewindTo` / arena chunk 管理；
     - `IdentifierTable::Intern` 的 FNV-1a/xxhash 哈希、扩容、回退；
     - `TokenPool::Add/Get/Save/Restore/Reset/Stats` 的全部方法体；
     - `SharedTokenPool` 的方法体。
   - 不允许保留 `// TokenPool is header-only for now.` 之类占位注释；不允许 `// TODO`；不允许任何空函数体或 `return {};` 兜底。

4.前端接入（必须真实使用，否则视同未完成）：
   - 修改全部 9 个前端的 lexer/parser，把内部 `std::vector<Token>` / 字符串拷贝改为通过 `TokenPool` 与 `StringArena` 管理：
     - `frontends/cpp/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/python/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/java/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/dotnet/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/rust/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/go/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/javascript/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/ruby/src/lexer/lexer.cpp` + `parser/parser.cpp`
     - `frontends/ploy/src/lexer/lexer.cpp` + `parser/parser.cpp`
   - 修改 `frontends/common/src/preprocessor.cpp`：宏展开产生的 token 必须 push 到调用方传入的 `TokenPool`，`#define` 体的字面量走 arena intern，禁止再用 `std::string` 拷贝。
   - 修改 `frontends/common/src/frontend_registry.cpp` 与 `frontends/common/include/language_frontend.h`：在 `FrontendOptions` / `FrontendResult` 中加入 `TokenPool *token_pool`（由 polyc/polyui 调用方持有，可选；为 `nullptr` 时前端自建私有 pool 保持向后兼容）。
   - 修改 `tools/polyc/src/stage_frontend.cpp` 与 `tools/ui/common/src/compiler_service.cpp`：编译会话级别复用一个 `SharedTokenPool`，编译完成后调用 `Stats()` 写入 aux 诊断目录 `pool_stats.json`。

5.诊断与调试：
   - `Diagnostics` 中所有引用 token 的 API 改为接收 `TokenHandle`，渲染时通过 pool 反查；保证多文件链上诊断不会因为 `Token&` 失效而崩溃。
   - 新增 `polyc --dump-token-pool` CLI 标志：编译完成后将 `pool_stats.json` 与 `tokens.tsv`（handle / kind / lexeme / loc）落盘到 aux 目录，便于性能调优与回归调试。
   - `polyui` 状态栏显示当前会话的 `tokens / arena_bytes / unique_identifiers`，悬浮 tooltip 给出 intern 命中率。

6.CLI 与设置联动（与 2026-04-27-4 设置体系对齐）：
   - 新增设置项（写入 `default_settings.json` 与 `settings_schema.json`）：
     - `frontend.tokenPool.arenaChunkBytes`（int，默认 65536，最小 4096，最大 16777216）
     - `frontend.tokenPool.shared`（bool，默认 `true`，控制是否使用 `SharedTokenPool`）
     - `frontend.tokenPool.dumpStats`（bool，默认 `false`，等价于 `--dump-token-pool`）
   - 三项均通过现有 `SettingsService` 加载，CLI 标志优先级高于 settings。

7.测试（不允许 `REQUIRE(true)`，必须行为级断言）：
   - 改写 `tests/unit/frontend/preprocessor_tests.cpp` 中现有 3 个 `TokenPool` 用例，断言新增 API 行为。
   - 新增 `tests/unit/frontend/token_pool_test.cpp`，至少覆盖：
     - `StringArena::Intern` 跨多个 chunk 的稳定性（保存 1000 个 string_view，再分配 100MB 后断言全部仍指向正确字节）；
     - `IdentifierTable` 命中率与扩容正确性（10w 个标识符，含 1w 重复）；
     - `TokenPool::Save/Restore` 在 lookahead 失败后能精确回退 token 数、arena offset、identifier 数；
     - `SharedTokenPool` 在 16 线程下并发 `InternIdentifier` 的 id 唯一性与稳定性（TSan 通过）；
     - `TokenHandle` 在 1M 次 `Add` 后仍稳定（`Get(h)` 与首次返回内容字节级一致）。
   - 新增 `tests/integration/frontend/token_pool_pipeline_test.cpp`：调用 `polyc` 对 `tests/samples` 中的 cpp / python / rust / java / .net / ploy 各一份样例编译，断言 `pool_stats.json` 字段齐全、`intern_hits > 0`、`unique_identifiers > 0`、编译成功；并对同一组文件再编译一次以验证 `Reset()` 复用路径。
   - 新增 `tests/benchmarks/micro/token_pool_bench.cpp`：对比"私有 pool"与"共享 pool"在 100k token 词法化下的吞吐与峰值内存（通过 `tests/benchmarks/macro` 已有的内存统计 hook）；接入现有 `benchmark_fast` / `benchmark_full` 档位（参见 2026-02-22-4）。

8.文档（中英双语）：
   - 新增 `docs/realization/token_pool.md` 与 `docs/realization/token_pool_zh.md`，包含：
     - 数据模型图（TokenPool / StringArena / IdentifierTable / SharedTokenPool 之间关系）；
     - `TokenHandle` 稳定性契约；
     - `Save/Restore` 的 lookahead 用法示例；
     - 9 个前端的接入方式与改动清单；
     - 设置项与 CLI 标志参考表；
     - 性能基准对比（微基准数字、内存对比）；
     - 故障排查（intern 命中率过低、arena 增长异常、并发数据竞争）。
   - 更新 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md`：在 "Compilation Pipeline" / "编译管线" 章节加入 token pool 子节，列出新设置项与 `--dump-token-pool` 用法。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：新增 `TokenPool` / `SharedTokenPool` / `StringArena` / `IdentifierTable` / `TokenHandle` / `SymbolId` 公共 API。
   - 更新 `docs/specs/namespace_architecture.md`：把 `TokenPool` 旁边补齐 `SharedTokenPool` / `StringArena` / `IdentifierTable`。
   - 更新 `README.md`：在 Architecture 段落补充"shared frontend token pool with arena & interning"。

9.约束：
   - 不允许最小实现 / 占位 / 空函数体（如 `// TokenPool is header-only for now.` 这类必须被彻底替换）；
   - C++ 代码注释一律英文；
   - 所有公共类型放在 `polyglot::frontends` 命名空间，文件命名小写下划线，与现有 `lexer_base.h` / `preprocessor.h` 风格一致；
   - 哈希实现走 `<functional>` / 项目已有的 `common/util` 哈希工具，禁止引入新依赖；
   - 必须保证 `--ploy` / `--cpp` / `--python` / `--java` / `--dotnet` / `--rust` / `--go` / `--javascript` / `--ruby` 全部 9 个前端在 `unit_tests` / `integration_tests` 中继续通过；
   - 现有所有单元 / 集成 / 基准测试不得回归；
   - 必须在 Windows / Linux / macOS 三平台编译并通过 CI（含 ASan / TSan / clang-tidy 闸门，参见 2026-03-17-8 与 2026-03-19-1）；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因引入新的公共类型 (`TokenHandle` / `SymbolId` / `SharedTokenPool` / `StringArena` / `IdentifierTable`) 与 `Token::lexeme` 类型变更（`std::string` → `string_view`，对前端是不兼容修改，对外只是 ABI 内部细节），视作内部 API 重构而非用户面向变更；建议版本号在 `1.2.0 → 1.2.1`（patch 级；若与 2026-04-27-5 同发布周期合并，则统一为 `1.3.1`）。

--end -done

2026-04-28-2

背景：审查 `backends/` 后发现整个后端层的抽象不足、目标间能力严重不对称、关键基础设施缺位，已经无法支撑后续多架构演进与质量闸门要求。具体观察：

- `backends/common/include/target_machine.h` 仅 24 行，接口只暴露 `TargetTriple()` 与 `EmitAssembly()`，没有 `EmitObject` / 重定位汇报 / 符号导出 / Verifier / 诊断；
- `abi.h` (22 行) 仅 `{name, pointer_size}`、`relocation.h` (26 行) 只有 `{kAbs32, kAbs64, kPcRel32}` 三种，缺 GOT/PLT/TLS/ARM64 `R_AARCH64_ADR_PREL_PG_HI21`/WASM reloc 全族；
- `polyc` 当前按架构字符串走长 if/else 分发后端，尚未沉淀 `BackendRegistry`/`ITargetBackend`（与 2026-03-17-2 在前端层已修复的问题同构）；
- x86_64 与 arm64 各自一份 `MachineIR` / `LinearScan` / `GraphColoring` / `Scheduler` / `CostModel`（两份 `scheduler.cpp` ~95% 同源），算法/数据结构重复实现；
- x86_64 有 `optimizations.cpp` (1373 行) 与 `instruction_scheduler.h`，arm64 完全缺失，后端能力跨目标不对称；
- `backends/wasm/src/wasm_target.cpp` 是 962 行单体，没有按 isel/regalloc/asm_printer 切分，与其他后端目录结构不一致；
- 无 RISC-V 后端，无 LLVM bitcode 备用输出；
- `debug_info.cpp` (41 行) / `debug_emitter.cpp` (1286 行) / `dwarf_builder.cpp` (236 行) 职责分裂，且 `debug_emitter.cpp` 仍保留 7 处 `WriteLE<uint32_t>(result, 0); // Placeholder for length` 与多处 `// Placeholder` / `// Simplified` 残留（grep 已确认）；
- 无 MachineIR Verifier，isel/regalloc 后无合法性校验；
- `object_file.cpp` (544 行) 仅支持 ELF 与最小 Mach-O，没有 COFF 写入路径；
- ARM64 `calling_convention.cpp` (201 行) 只覆盖基础 GPR 与少量 FP，没有 NEON/SVE 参数传递规则。

本需求要求把后端层做一次结构性重构与能力补齐，定位为 1.4.0 主线特性。具体要求如下：

1.目标机抽象与后端注册中心：
   - 在 `backends/common/include/target_backend.h` 新增 `class ITargetBackend`，明确接口面：
     - `TargetTriple()` / `Description()` / `IsAvailable()`；
     - `Verify(const ir::IRContext &) -> Diagnostics`；
     - `Compile(const ir::IRContext &, const TargetOptions &) -> Result<TargetArtifacts>`，其中 `TargetArtifacts` 至少包含：`assembly_text`、`object_bytes`、`relocations`、`exported_symbols`、`unresolved_symbols`、`debug_sections`、`compile_stats`；
     - `EmitObject(...)`、`EmitAssembly(...)`、`EmitBitcode(...)`（最后一个允许返回"unsupported"诊断）；
     - `RegisterClasses() const` / `CallingConventions() const` / `RelocationTraits() const`：暴露目标元数据，供链接器与诊断器查询。
   - 新增 `class BackendRegistry`（单例 + 进程级互斥）：
     - `Register(std::unique_ptr<ITargetBackend>)`；
     - `Find(string_view triple_or_alias) -> ITargetBackend*`；
     - `List() -> std::vector<BackendInfo>`（含 triple、aliases、capability matrix）。
   - x86_64 / arm64 / wasm 三个现有后端必须改造为 `ITargetBackend` 实现并在 `static initializer` 中自注册；后续新增后端只增不改 `polyc`。
   - 修改 `tools/polyc/src/driver.cpp`：删除按架构字符串硬分发的 if/else 长链，改为统一走 `BackendRegistry::Find(opts.target_triple)`；当 triple 解析失败或 backend 不可用时，给出"available backends"列表的诊断（参考 LLVM `--print-targets`）。
   - 新增 `polyc --print-targets` / `--print-target-info <triple>` CLI，输出当前进程注册的后端能力矩阵（JSON 与人类可读两种）。
   - 修改 `tools/polyasm/src/assembler.cpp`：同步走 `BackendRegistry`，禁止保留独立的架构枚举分支。

2.通用 MachineIR 与 codegen 通用算法上提：
   - 新增 `backends/common/include/machine_ir/`：`opcode_traits.h` / `machine_function.h` / `live_interval.h` / `cost_model.h` / `verifier.h`；提供与目标无关的 `MachineFunction<TargetTraits>` 模板（或基于 `TargetTraits` 的 CRTP/policy 设计），把 `MachineBasicBlock` / `MachineInstr` / `Operand` 通用结构沉淀到此处。
   - 把 `LinearScanAllocate` / `GraphColoringAllocate` / `ComputeLiveIntervals` / `ScheduleFunction` 上提到 `backends/common/src/machine_ir/`，以模板/虚接口接受目标级 `RegisterClass` 与 `CostModel`；x86_64 与 arm64 各自只保留目标特异部分（register class 表、call clobber、ABI 约束）。
   - **强制**：上提后 `backends/x86_64/src/regalloc/*.cpp`、`backends/arm64/src/regalloc/*.cpp`、两份 `scheduler.cpp` 必须真正删除（用 `git rm`，禁止保留"deprecated wrapper"），剩余目标特定文件不得再持有完整算法实现。
   - 新增 `backends/common/include/machine_ir/verifier.h` + 实现：
     - 校验：(a) 所有 use 在 def 之后；(b) BB 必须以 terminator 结尾；(c) physical register 与 register class 兼容；(d) stack slot 尺寸合法；(e) call 指令的实参寄存器与目标 ABI 一致。
     - 在 isel 后、regalloc 前后各跑一次；任何 verify 失败必须给出 `MachineIR` 文本快照（`MachineFunction::Print()`）与失败位置，并在非 `--force` 模式下中断编译。

3.ABI / 重定位 / 调用约定模型完整化：
   - 重写 `abi.h` 为完整模型：`struct ABISpec { string name; size_t pointer_size; size_t int_reg_param_count; vector<RegId> int_arg_regs; vector<RegId> fp_arg_regs; bool return_in_x0_x1; StructPassingRule struct_rule; ... };`；至少覆盖 `SystemV-x86_64` / `Win64-x86_64` / `AAPCS64` / `Win-ARM64` / `Wasm-Default` 五套预设。
   - 重写 `relocation.h` 为按目标分文件：
     - `relocation_x86_64.h`（含 `R_X86_64_PC32` / `R_X86_64_PLT32` / `R_X86_64_GOTPCREL` / `R_X86_64_TPOFF32` / `R_X86_64_TLSGD` 等全集）；
     - `relocation_arm64.h`（含 `R_AARCH64_CALL26` / `R_AARCH64_ADR_PREL_PG_HI21` / `R_AARCH64_ADD_ABS_LO12_NC` / `R_AARCH64_LDST64_ABS_LO12_NC` 等）；
     - `relocation_wasm.h`（含 `R_WASM_FUNCTION_INDEX_LEB` / `R_WASM_TABLE_INDEX_SLEB` / `R_WASM_MEMORY_ADDR_LEB` 等）；
     - 通用 `relocation.h` 改为聚合头与跨目标 trait 接口，禁止再保留 `enum class RelocType { kAbs32, kAbs64, kPcRel32 };` 这种过简枚举。
   - 链接器侧（`tools/polyld/src/polyglot_linker.cpp` 等）必须按新重定位模型消费，不允许保留按字符串匹配的旧路径。

4.WASM 后端拆分与能力补齐：
   - 把 `backends/wasm/src/wasm_target.cpp` (962 行) 拆为目录化结构（与 x86_64 对齐）：`isel/`、`regalloc/`、`asm_printer/`（emitter 写 `.wat` 与 `.wasm` 两种产物）、`optimizations/`、`calling_convention.cpp`。
   - 实施真正的结构化 CFG → block-depth 映射（修复 2026-03-15-4 第 6 行的 `br/br_if` 深度固定 0 的遗留），并对每个 IR 指令显式给出"已支持/通过 runtime helper 模拟/拒绝"三态决策。
   - `frem` 必须走 `f64.div + trunc + sub`（或 runtime helper），禁止继续以 `f64.div` 单条指令冒充语义（修复 2026-03-15-4 第 7 行）。
   - 未解析 callee 走"emit import + 记录 unresolved 符号"路径，禁止继续 emit `index 0` 占位（修复 2026-03-15-4 第 5 行）。

5.RISC-V 后端首版（rv64gc）：
   - 新增 `backends/riscv64/`：目录与 x86_64/arm64 完全对齐（`include/`、`src/isel/`、`src/regalloc/`（仅薄 register class 适配）、`src/asm_printer/`、`src/calling_convention.cpp`、`src/optimizations.cpp` 可暂留 stub 但必须能跑通 lit 级 e2e）。
   - 通过 `BackendRegistry` 暴露 triple `riscv64-unknown-elf` 与 `riscv64-linux-gnu`。
   - 至少完成：算术/逻辑/分支/调用/load/store/`auipc`+`addi` 地址计算、PC 相关重定位（`R_RISCV_CALL_PLT`、`R_RISCV_PCREL_HI20`、`R_RISCV_PCREL_LO12_I`），通过新增的 e2e smoke 测试。
   - 不允许只做"目录骨架"提交：必须能编译并运行 `tests/samples/01-basic-linking` 中至少一个 sample 到 `.o`，并能与 polyld 走通到可执行（在 CI 上以 `qemu-riscv64` 运行）。

6.LLVM bitcode 备用输出：
   - 新增 `backends/common/src/bitcode_emitter.cpp`（依赖 `nlohmann/json` 之外的零额外重量级库；若必须依赖 LLVM，须在 CMake 中以 `POLYGLOT_ENABLE_LLVM_BITCODE` 选项守护，默认 OFF，但接口与诊断必须在 OFF 时仍可调用并给出明确不可用诊断）。
   - 提供 `polyc --emit=bitcode` 与 `polyc --emit=llvm-ir` 两个新发射模式；用于与外部工具链互通（perf/coverage/外部链接），并在文档中说明 bitcode 不参与跨语言链接闭环、仅用于研究/对比。

7.Debug 信息层归一化与占位清零：
   - 合并 `debug_info.cpp` (41 行) / `debug_emitter.cpp` (1286 行) / `dwarf_builder.cpp` (236 行) 为单一职责清晰的两个组件：`debug_info_model`（IR 端 → 中性 debug 模型）与 `debug_emitter`（中性模型 → DWARF/PDB 字节流）；删除重复的 SourceLocation 定义（`debug_info_builder.h` 与 `dwarf5.h` 的重复见 2026-02-21-7 #1）。
   - **强制**：清理 `debug_emitter.cpp` 中所有 `// Placeholder` / `// Placeholder for length` / `// Simplified` 注释及其覆盖的代码：
     - PDB stream 长度字段必须二次回写真实长度，禁止留 0；
     - CFA 初始化必须按 ABI 真实计算，不允许"address++ // Simplified address tracking"；
     - GUID 必须使用 `<random>` + 时间戳 + 模块哈希组合生成，不允许保留"GUID generation (simplified)"；
     - PE/COFF 调试目录、TPI/IPI type record 必须按 PDB v7 spec 真实序列化。
   - 新增 `tests/integration/backends/debug_emitter_e2e_test.cpp`：用 `dwarfdump` / `llvm-pdbutil`（在 CI 容器中安装）解析产物，断言段长度、行号表、变量位置、函数 DIE 数量。

8.对象文件写入扩展：
   - 在 `backends/common/src/object_file.cpp` 增加 COFF 写入路径（含 `.text` / `.rdata` / `.pdata` / `.xdata` / debug `$T` 段），与 ELF/Mach-O 三平台对齐。
   - ELF 的 `e_machine` 必须按目标 triple 切换（修复 2026-02-21-7 #2 中 `EM_X86_64` 硬编码遗留），并把 `SHT_RELA` section 真正落地（reloff/nreloc 与 Mach-O 的 nreloc 同步）。

9.ARM64 ABI 补齐：
   - 在 `backends/arm64/src/calling_convention.cpp` 中按 AAPCS64 完整规则覆盖：
     - HFA / HVA 检测与按 v0..v7 传参；
     - 大于 16 字节的复合类型按指针 + 隐式 hidden return ptr (`x8`) 传递；
     - NEON 128-bit 向量参数；
     - 可选 SVE scalable vector：在 backend 端增加 capability flag，未启用时显式诊断"sve not enabled"。
   - 新增对应单元测试矩阵（每条规则 ≥ 1 个用例）。

10.诊断与产物元信息：
   - `TargetArtifacts` 必须随产物输出 `aux/<basename>.target.json`：含目标 triple、ABI 名、寄存器分配策略、未解析符号清单、所有发射的重定位条目、调试段大小、isel/regalloc/sched 各阶段耗时与峰值内存。
   - polyc 与 polyui 编译完成后均须把该 JSON 摘要回显（polyui 在状态栏与 Build Output 面板）。

11.设置项与 CLI 联动（与 2026-04-27-4 设置体系对齐）：
   - 新增设置（写入 `default_settings.json` 与 `settings_schema.json`，分发到 `SettingsService`）：
     - `backend.regAlloc`（enum：`linear-scan` | `graph-coloring`，默认 `linear-scan`）；
     - `backend.scheduler`（enum：`list` | `none`，默认 `list`）；
     - `backend.verify`（enum：`off` | `on` | `strict`，默认 `on`，`strict` 等价 `--Werror-machine-ir`）；
     - `backend.emit`（enum：`object` | `assembly` | `bitcode` | `llvm-ir`，默认 `object`）；
     - `backend.debugInfo`（enum：`none` | `line` | `full`，默认 `full`）。
   - CLI 标志：`--reg-alloc`、`--scheduler`、`--verify-machine-ir={off|on|strict}`、`--emit=<...>`、`--debug=<...>`，CLI 优先级高于 settings。

12.测试（不允许 `REQUIRE(true)`，必须行为级断言）：
   - 新增 `tests/unit/backends/target_backend_registry_test.cpp`：覆盖注册、查找、能力矩阵、重复注册诊断。
   - 新增 `tests/unit/backends/machine_ir_verifier_test.cpp`：构造刻意非法的 MIR（use-before-def / 缺 terminator / 错 register class），断言 verifier 正确诊断且产生定位信息。
   - 新增 `tests/unit/backends/abi_x86_64_test.cpp` / `abi_arm64_test.cpp`：对照 SystemV / Win64 / AAPCS64 规范实例（含结构体、HFA、可变参数）。
   - 新增 `tests/unit/backends/relocation_test.cpp`：对每族重定位至少 1 个用例，校验 reloc 编码字节级一致。
   - 新增 `tests/unit/backends/riscv64_basic_test.cpp`：算术/分支/调用/load-store 各 ≥ 1 个用例。
   - 新增 `tests/integration/backends/multi_target_e2e_test.cpp`：同一段 IR 分别经由 x86_64 / arm64 / riscv64 / wasm 四个后端走完整链路（emit object → polyld → 运行/`qemu` 验证），输出一致；wasm 走 `wasmtime` 在 CI 中执行。
   - 把现有 `tests/unit/backends/wasm_target_test.cpp` 中 `simplified` 类注释覆盖的弱断言改为：解析产物 wasm 字节流并断言指令序列、block 深度、import 表。
   - 微基准：新增 `tests/benchmarks/micro/regalloc_bench.cpp` 对比 linear-scan 与 graph-coloring 在 1k / 10k 虚拟寄存器规模下的吞吐与峰值内存。

13.文档（中英双语）：
   - 新增 `docs/realization/backend_architecture.md` 与 `docs/realization/backend_architecture_zh.md`，包含：
     - `ITargetBackend` 接口契约与生命周期；
     - `BackendRegistry` 注册机制与与 `FrontendRegistry` 的对照；
     - 通用 MachineIR 与目标 trait 的设计；
     - ABI / Reloc 模型分类参考表；
     - 各目标能力矩阵（已支持 / 部分支持 / 未实现）；
     - WASM 拆分后的目录结构；
     - RISC-V 首版能力边界与运行依赖（qemu-riscv64）；
     - Debug emitter 重构与 PDB / DWARF 段布局参考；
     - 故障排查（verify 失败诊断、目标找不到、bitcode 路径不可用）。
   - 新增 `docs/specs/target_backend_spec.md` 与 `_zh.md`：单文档单源化"目标后端开发者规范"，给出新增后端的最小步骤清单与必须通过的测试集。
   - 更新 `docs/USER_GUIDE.md` / `_zh.md`：在"Backends"章节列出新设置项与 CLI 标志，给出 `--print-targets` 输出示例与 RISC-V 运行示例。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：新增 `ITargetBackend` / `BackendRegistry` / `MachineFunction` / `RegisterClass` / `ABISpec` / `RelocationTraits` / `TargetArtifacts` 公共 API。
   - 更新 `docs/specs/namespace_architecture.md`：在 `polyglot::backends` 下补齐 `target_backend` / `backend_registry` / `machine_ir`。
   - 更新 `README.md`：在 Architecture 段落补充"backend registry, RISC-V target, machine IR verifier, WASM split"。

14.约束：
   - 不允许最小实现 / 占位 / 空函数体；明令禁止再向后端代码引入 `// Placeholder` / `// Simplified` / `// TODO` 注释（新增 CI 校验：`grep -rn -E "Placeholder|Simplified|TODO" backends/` 必须返回 0 行）；
   - C++ 代码注释一律英文；
   - 命名空间一律 `polyglot::backends` 与 `polyglot::backends::<target>`；文件命名小写下划线；
   - 不允许保留两份重复算法（regalloc / scheduler 必须落到 `backends/common`）；
   - WASM 拆分后旧的 962 行单文件必须 `git rm`，禁止保留"deprecated"备份；
   - 现有所有单元 / 集成 / 基准测试必须仍然通过；
   - 必须在 Windows / Linux / macOS 三平台编译并通过 CI（含 ASan / UBSan / TSan / clang-tidy 闸门，参见 2026-03-17-8 与 2026-03-19-1）；
   - RISC-V 与 WASM e2e 在 CI 中需以 `qemu-riscv64` / `wasmtime` 执行，禁止仅做"emit 通过即算成功"；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因引入 `ITargetBackend` / `BackendRegistry` / 新增 RISC-V 目标 / Reloc 模型重写 / WASM 目录结构变更（对外是新能力，对内是较大重构），建议版本号 `1.3.0 → 1.4.0`（次版本号 +1，向后兼容；CLI 旧 `--target=x86-64` 等别名通过 `BackendRegistry` 的 alias 机制保持有效）。

--end

注：2026-04-28-2 作为伞形需求条目，覆盖面过大（约 70 个文件、~20K LOC 代码 + 6 篇双语文档），单次会话无法在不违背"禁止最小实现/占位"约束的前提下完整交付。经与 MC 协商，按 Plan A 拆为 7 个串行子需求 2026-04-28-2a … 2026-04-28-2g（见下方独立条目），每个子需求为可单独编译、测试、文档化、版本化的最小闭环，伞形条目不单独追加 `--end -done`，而是当 2a–2g 全部追加 `--end -done` 时视作伞形完成。版本节奏：2a..2f 走 `1.3.3 → 1.3.8` patch 级递进，2g 收口时统一 `1.3.8 → 1.4.0` 次版本跃迁。

2026-04-28-2a

背景：从伞形 2026-04-28-2 中拆出，对应原 §1。先行打通"目标机抽象 + 后端注册中心 + 工具链分发"骨架，作为后续所有子需求（MachineIR 上提、ABI/Reloc 重写、WASM 拆分、RISC-V 接入、Debug 归一化）的注册基座。本子需求只增不删现有后端实现，零回归切换 polyc/polyasm 的分发路径。

1.目标机抽象与后端注册中心：
   - 新增 `backends/common/include/target_backend.h`：定义 `ITargetBackend` 抽象接口，及 `TargetOptions` / `TargetArtifacts` / `BackendCapabilities` / `BackendInfo` / `MCRelocation` / `MCSymbol` / `MCSection` / `CompileStats` / `BackendDiagnostic` / `CompileResult` 完整数据模型；提供 `EmitAssembly` / `EmitObject` / `EmitBitcode` 三个发射入口（`EmitBitcode` 默认返回 `unsupported` 诊断，留待 2026-04-28-2e 启用）。
   - 新增 `backends/common/include/backend_registry.h` + `src/backend_registry.cpp`：进程级单例，提供 `Register` / `Find` / `FindOrDiagnose` / `List` / `Size` / `Clear`、`RegisterStatus` 错误枚举、`BackendRegistrar` RAII、`REGISTER_TARGET_BACKEND` 宏，以及 `ToJson` / `ToHumanReadable` 序列化器。
   - 三个现有后端 (x86_64 / arm64 / wasm) 必须通过 adapter（`x86_target_backend.cpp` / `arm64_target_backend.cpp` / `wasm_target_backend.cpp`）实现 `ITargetBackend` 并在静态注册期自注册；不允许修改现有 `X86Target` / `Arm64Target` / `WasmTarget` 的对外接口（避免引入横向回归，留给后续子需求处理）。
   - 别名解析必须大小写不敏感（与 `FrontendRegistry` 一致），至少覆盖：x86_64 → `[x86_64, x86-64, amd64, x64, x86_64-pc-windows-msvc, x86_64-apple-darwin, x86_64-linux-gnu]`；arm64 → `[arm64, aarch64, armv8, aarch64-apple-darwin, aarch64-linux-gnu, aarch64-pc-windows-msvc]`；wasm → `[wasm, wasm32, wasm64, wasm32-wasi, wasm-unknown-unknown]`。

2.工具链分发改造：
   - `tools/polyc/src/compilation_pipeline.cpp`：删除按 `arch` 字符串走 if/else 的分发链，统一改为 `BackendRegistry::Instance().FindOrDiagnose(target, &diag)` → `backend->Compile(ir_module, opts)` → `TargetArtifacts` 翻译为现有 `CompiledObject` / `linker::Symbol` / `linker::Relocation`；查找失败必须给出"available backends"列表诊断。
   - `tools/polyasm/src/assembler.cpp`：同步改造，禁止保留独立的架构枚举分支（WASM 二进制直接落盘的快路径可保留）。
   - `tools/polyc/src/driver.cpp`：在 `main()` 入口提供 `--print-targets[=json|text]` 与 `--print-target-info=<triple>[:json]` 两个 CLI，输出注册表快照（人类可读 + JSON 两种），用法仿 LLVM `--print-targets`；`--help` 文本同步更新。

3.测试：
   - 新增 `tests/unit/backends/target_backend_registry_test.cpp`：≥ 7 个用例覆盖：三后端自注册、大小写不敏感的别名解析、`List()` 排序快照与能力矩阵断言、重复 triple/别名冲突/空指针注册的拒绝路径、`FindOrDiagnose` 失败诊断的内容、JSON + 人类可读序列化、`EmitBitcode` 默认 unsupported 诊断。
   - 现有 `test_backends` / `test_core` / `test_middle` / `test_runtime` / `test_linker` 全部不得回归。

4.文档（中英双语）：
   - 新增 `docs/realization/backend_registry.md` 与 `docs/realization/backend_registry_zh.md`，覆盖：`ITargetBackend` 契约与生命周期、`BackendRegistry` 与 `FrontendRegistry` 对照、别名解析规则、能力矩阵、JSON schema、polyc/polyasm 分发迁移路径、`--print-targets` / `--print-target-info` 输出示例与故障排查。
   - 更新 `docs/specs/namespace_architecture.md` / `_zh.md`：在 `polyglot::backends` 行补齐 `ITargetBackend` / `BackendRegistry`。
   - 更新 `docs/USER_GUIDE.md` / `_zh.md`：在 "Backends" / "后端" 章节加入 `--print-targets` 与 `--print-target-info` 的使用说明与示例输出。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：新增 `ITargetBackend` / `BackendRegistry` / `TargetOptions` / `TargetArtifacts` / `BackendInfo` / `BackendCapabilities` 公共 API 表项。

5.约束：
   - 不允许最小实现 / 占位 / 空函数体；
   - C++ 代码注释一律英文；
   - 公共类型一律 `polyglot::backends` 命名空间，目标特异 adapter 放 `polyglot::backends::<target>`，文件命名小写下划线；
   - `ITargetBackend` 与 `BackendRegistry` 的设计必须与 `polyglot::frontends::FrontendRegistry` 风格一致，便于未来工具复用；
   - 现有 x86_64 / arm64 / wasm 后端的 .cpp 文件不得删除（留给 2b / 2c / 2d 子需求处置）；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅新增基础设施 + 改造分发路径，对外 CLI 仅新增 `--print-targets` / `--print-target-info`（向后兼容），版本号 `1.3.2 → 1.3.3`（patch 级）。

--end -done

2026-04-28-2b

背景：从伞形 2026-04-28-2 中拆出，对应原 §2。基线审查显示 `backends/x86_64/include/machine_ir.h` (178 行) 与 `backends/arm64/include/machine_ir.h` (160 行) 除目标特定的 `Opcode` 与 `Register` 默认值外完全同源；`linear_scan.cpp` (各 121 行) / `graph_coloring.cpp` (各 84 行) / `asm_printer/scheduler.cpp` (各 84 行) 在两个后端中字节级一致（仅命名空间不同）。这意味着任何 regalloc / scheduler 修复都要改两遍，且没有任何机制保证两份不漂移。本子需求把这些算法上提到 `backends/common/`，删除重复实现，并加入 MachineIR Verifier，作为后续 2c (ABI/Reloc) 与 2d (WASM 拆分) 的清洁基座。

1.通用 MachineIR 模板：
   - 新增 `backends/common/include/machine_ir/machine_ir.h`，提供与目标无关的模板：`Operand<TargetTraits>` / `MachineInstr<TargetTraits, OpcodeT>` / `MachineBasicBlock<TargetTraits, OpcodeT>` / `MachineFunction<TargetTraits, OpcodeT>` / `LiveInterval<TargetTraits>` / `AllocationResult<TargetTraits>` 与 `enum class RegAllocStrategy`。`TargetTraits` 至少暴露 `using Register = ...;` 与 `static constexpr Register kDefaultRegister = ...;`。
   - 上述头文件中以函数模板形式同时提供 `ComputeLiveIntervals`、`LinearScanAllocate`、`GraphColoringAllocate`、`ScheduleFunction` 的实现 —— 算法与目标无关，只通过 `Register` 的值传递使用。实现必须与原 x86_64 版本字节等价，回归保护必须可由现有测试套件确认。
   - 新增 `MachineFunction::Print()` 自由模板函数：渲染 `function name {... bb name: instr [opcode-index def=N uses=[…] term=Y/N] …}` 的人类可读快照，供 Verifier 失败诊断使用。

2.per-target `machine_ir.h` 改造：
   - x86_64 与 arm64 的 `machine_ir.h` 仅保留：目标特定 `Opcode` 枚举、`<Target>TargetTraits`、`CostModel` 与 `SelectInstructions` 声明，其余类型一律 `using` 自 `backends/common/include/machine_ir/machine_ir.h` 的模板实例。
   - 为保持现有 `isel.cpp` / `emit.cpp` / `calling_convention.cpp` / `arm64_target_backend.cpp` / `x86_target_backend.cpp` 等消费者**调用点零变更**，必须以薄 inline 包装函数桥接 `ComputeLiveIntervals` / `LinearScanAllocate` / `GraphColoringAllocate` / `ScheduleFunction`（保留与旧版完全一致的非模板签名）。

3.MachineIR Verifier：
   - 新增 `backends/common/include/machine_ir/verifier.h` 与对应（如有需要的）实现：`MachineIRVerifier<TargetTraits, OpcodeT>` 提供 `std::vector<Diagnostic> Verify(const MachineFunction&)`，覆盖 (a) use 必须在同一 BB 的某条 def 之后或来自函数入口；(b) 每个 BB 必须以 `terminator==true` 的指令结尾；(c) `MachineInstr::def < 0` 与 `terminator==true` 不可同时既无 def 也无 uses 且非 `kRet/kJmp/kJcc` 等终止 opcode。
   - 失败诊断必须携带 `function_name`、`block_name`、`instruction_index` 与一条 `MachineFunction::Print()` 快照位置标记。
   - ABI / register-class 兼容性 / stack slot 尺寸合法性等更深检查留给后续子需求 2c（依赖新的 ABI 模型）；本子需求只交付与 ABI 解耦的两条核心规则与一条结构性规则。

4.重复源码删除：
   - 强制 `git rm`：
     - `backends/x86_64/src/regalloc/linear_scan.cpp`
     - `backends/x86_64/src/regalloc/graph_coloring.cpp`
     - `backends/x86_64/src/asm_printer/scheduler.cpp`
     - `backends/arm64/src/regalloc/linear_scan.cpp`
     - `backends/arm64/src/regalloc/graph_coloring.cpp`
     - `backends/arm64/src/asm_printer/scheduler.cpp`
   - `backends/CMakeLists.txt` 同步移除上述源文件，加入新的 common 头文件依赖（如 verifier 需 .cpp 实现则一并加入 `backend_common`）。
   - 不允许保留 deprecated wrapper 或备份；删除即彻底删除。

5.测试：
   - 新增 `tests/unit/backends/machine_ir_verifier_test.cpp`：≥ 5 用例覆盖：(a) 合法函数零诊断；(b) 缺 terminator 的 BB 命中规则 (b)；(c) 同 BB use-before-def 命中规则 (a)；(d) 跨 BB 的 use 视为已定义不报；(e) 空函数/空 BB 不崩溃。
   - 新增 `tests/unit/backends/machine_ir_template_test.cpp`：≥ 4 用例验证模板实例化在 x86_64 / arm64 两种 traits 下均能编译并产出与原算法字节等价的 `LinearScanAllocate` / `GraphColoringAllocate` / `ScheduleFunction` 结果（用刻意构造的小函数对比 vreg→phys 映射）。
   - 既有 `test_backends` / `test_core` / `test_middle` / `test_runtime` / `test_linker` 全部不得回归。

6.文档（中英双语）：
   - 新增 `docs/realization/machine_ir.md` 与 `docs/realization/machine_ir_zh.md`，覆盖：模板设计与 `TargetTraits` 协议、保留 per-target `Opcode` 与 `CostModel` 的理由、Verifier 规则、为什么删除 6 份 .cpp、新增后端如何接入、故障排查（verify 失败 / 模板实例化错误）。
   - 更新 `docs/specs/namespace_architecture.md` / `_zh.md`：在 `polyglot::backends` 行追加 `polyglot::backends::common::machine_ir` 子命名空间与新模板类型。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：在 §7.10 之后新增 §7.11 "Common MachineIR & Verifier"。

7.约束：
   - 不允许最小实现 / 占位 / 空函数体；
   - C++ 代码注释一律英文；
   - 公共类型一律 `polyglot::backends::common::machine_ir`，目标层 `using` 别名置于 `polyglot::backends::<target>`，文件命名小写下划线；
   - 算法必须与原 x86_64 / arm64 版本字节等价（回归测试保护）；
   - 6 份 .cpp 必须 `git rm`，禁止保留 deprecated 备份；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅做内部结构重构 + 新增 Verifier 公共接口（向后兼容），版本号 `1.3.3 → 1.3.4`（patch 级）。

--end -done

2026-04-28-2c

背景：从伞形 2026-04-28-2 中拆出，对应原 §3。基线审查显示：(i) `backends/common/include/abi.h` (24 行) 仅含一个 `struct ABI { name; pointer_size; }` 占位结构，全项目零消费者；(ii) `backends/common/include/relocation.h` (24 行) 仅含 `enum class RelocType { kAbs32, kAbs64, kPcRel32 }` + 一个三字段 `struct Relocation`，同样全项目零 include 消费者（`tools/polyc/src/compilation_pipeline.cpp:144` 使用的 `backends::Relocation` 来自 `object_file.h`，字段不同）；(iii) `backends/x86_64/src/calling_convention.cpp` (217 行) 与 `backends/arm64/src/calling_convention.cpp` (237 行) 在 `StackFrame` 字段集、`ComputeStackFrame` 算法骨架、`CallingConvention` 类方法签名上字节级同源，仅在寄存器表与汇编语法层不同；(iv) `target_backend.h` 已成熟提供 `MCRelocation { section, offset, type, symbol, addend }`，但与 `object_file.h::Relocation` 字段重复且无映射桥。本子需求把 ABI（调用约定 + 栈帧布局）抽象上提到 `backends/common/include/abi/`，并把"目标无关重定位语义 → 各 ELF/Mach-O 整数码"映射归一到 `backends/common/include/abi/relocation.h`，同步把现有占位文件 `abi.h` / `relocation.h` 改造为转发头（不删除文件，遵循禁止删库规则），为后续 2d (WASM 拆分) / 2e (Debug 归一化) / 2f (RISC-V 接入) 提供 ABI 接入面。

1.通用 ABI 模板：
   - 新增 `backends/common/include/abi/calling_convention.h`：定义 `polyglot::backends::common::abi::StackFrame<TargetTraits>` POD（字段集与现有 x86_64/arm64 私有 `StackFrame` 同源：`total_size / spill_area_size / local_area_size / arg_area_size / saved_regs`）；定义 `polyglot::backends::common::abi::CallingConvention<TargetTraits>` 模板类，至少暴露 `IntegerArgRegs() / FloatArgRegs() / CalleeSavedRegs() / VolatileRegs() / StackAlignment() / PointerSize() / RedZoneSize() / AvailableRegisters()` 共 8 个常量访问器，外加目标无关算法 `ComputeStackFrame(const MachineFunction&, const AllocationResult&)`（实现从 x86_64 版本字节等价上提）。`TargetTraits` 需在 2b 既有契约（`Register` + `kDefaultRegister`）之上追加：`static constexpr int kStackAlignment`、`static constexpr int kPointerSize`、`static constexpr int kRedZoneSize`、四张 `inline static const std::vector<Register>` 静态寄存器表（integer_arg / float_arg / callee_saved / volatile）。
   - 新增 `backends/common/include/abi/abi.h` 作为 `common::abi` 子命名空间的伞形头（聚合 `calling_convention.h` 与 `relocation.h`）。
   - 现有 `backends/common/include/abi.h` 不删除：内容重写为 `#include "backends/common/include/abi/abi.h"` + 在 `polyglot::backends` 命名空间提供 `using ABI = common::abi::AbiDescriptor;` 转发别名（`AbiDescriptor` 为新增的非模板汇总结构：`{ name; pointer_size; stack_alignment; red_zone_size; }`），保留旧 `struct ABI` 字段名向后兼容。

2.通用重定位语义：
   - 新增 `backends/common/include/abi/relocation.h`：定义目标无关 `enum class RelocationKind { kAbs32, kAbs64, kPcRel32, kPcRel64, kGotPcRel32, kPltPcRel32, kPage21, kPageOff12, kBranch26, kCondBranch19, kMovwG0Abs, kMovwG1Abs, kMovwG2Abs, kMovwG3Abs }`（同时覆盖 x86-64 SysV 与 AArch64 ELF/Mach-O 主流语义），并提供 `RelocationEntry { section; offset; kind; symbol; addend; }`（字段名/类型与既有 `MCRelocation` 一致以便互转）。
   - 提供 4 个目标专用映射函数：`std::uint32_t MapToElfX86_64(RelocationKind)` / `std::uint32_t MapToElfAArch64(RelocationKind)` / `std::uint32_t MapToMachOX86_64(RelocationKind)` / `std::uint32_t MapToMachOArm64(RelocationKind)`，覆盖 `R_X86_64_PC32 / R_X86_64_64 / R_X86_64_GOTPCREL / R_X86_64_PLT32 / R_AARCH64_ABS64 / R_AARCH64_ADR_PREL_PG_HI21 / R_AARCH64_ADD_ABS_LO12_NC / R_AARCH64_CALL26 / R_AARCH64_CONDBR19 / R_AARCH64_MOVW_UABS_G0..G3` 等；不可映射的组合返回 `0xFFFFFFFFu` 哨兵并在调用方走诊断路径。
   - 新增 `backends/common/src/abi.cpp`：上述 4 个映射函数 + `AbiDescriptor` 比较运算符 + `RelocationKind` 的 `ToString` / `ParseRelocationKind` 自由函数（用于诊断与未来 IR 序列化）。
   - 现有 `backends/common/include/relocation.h` 不删除：内容重写为 `#include "backends/common/include/abi/relocation.h"` + 在 `polyglot::backends` 命名空间提供 `using RelocType = common::abi::RelocationKind;` 与 `using Relocation = common::abi::RelocationEntry;` 转发别名。该转发不得破坏 `tools/polyc/src/compilation_pipeline.cpp` 当前对 `backends::Relocation`（来自 `object_file.h`）的引用 —— 通过 ADL/ namespace scoping 区分（`object_file.h` 中的 `Relocation` 仍位于 `polyglot::backends` 直接命名空间，但不与 `using Relocation =` 别名冲突，因为后者放在头部 include 守卫 + `inline namespace v2 {}` 保护层中；如出现 ODR 冲突则以条件编译 `#ifndef POLYGLOT_BACKENDS_RELOCATION_LEGACY` 退出转发别名，让旧 builder 类型继续优先）。

3.per-target `calling_convention.cpp` 改造：
   - x86_64 版本：删除文件内匿名命名空间的 `StackFrame` 与 `CallingConvention` 定义（实现已上提）；改为定义 `X86_64Traits` 实例化所需的 `inline static const std::vector<Register>` 4 张表 + 4 个常量 (`kStackAlignment=16, kPointerSize=8, kRedZoneSize=128`)；保留 `EmitPrologue` / `EmitEpilogue` / `EmitCallSetup` 三个汇编语法相关方法（作为 `CallingConvention<X86_64Traits>` 的 `friend` 自由函数 `EmitPrologueX86_64` / `EmitEpilogueX86_64` / `EmitCallSetupX86_64`）；保留 `GetSysVCallingConvention()` 与 `GetAvailableRegisters()` 两个外部符号（即便目前无外部消费者也保留，避免 ABI 表面变化）。
   - arm64 版本同上，改用 `Arm64Traits`，常量 `kStackAlignment=16, kPointerSize=8, kRedZoneSize=0`，保留 `EmitPrologueArm64` / `EmitEpilogueArm64` / `EmitCallSetupArm64`、`GetAAPCS64CallingConvention()`、`GetAvailableRegisters()`。
   - 改造后两个 .cpp 文件预计净减 ≥ 70 行（算法主体上提），不得保留任何注释形式的死代码；行数减少必须由测试确认零回归。

4.MachineIRVerifier ABI 规则扩展：
   - 在 `backends/common/include/machine_ir/verifier.h` 中新增可选 `AbiContract<TargetTraits>` 参数（默认 `nullptr`，向后兼容现有 5 个 verifier 测试）；当传入非空时启用第 (d) 条规则："`kCall` 指令的非 callee 操作数数量超过 `IntegerArgRegs().size() + FloatArgRegs().size() + 16` 时报告诊断 `kAbiCallArityExceeded`，提示需要栈传参且尺寸异常"。
   - 同步追加规则 (e)："函数体内任何 `kRet` 之前的最近一条 `kCall` 之后，到 `kRet` 之间，禁止读取已被 `EmitCallSetup` 视为 volatile 的物理寄存器"（仅在已分配阶段触发，未分配的 `kVReg` 操作数跳过此检查）。
   - 这两条规则的实现必须与 2b 既有 (a)/(b)/(c) 规则共存：当 `AbiContract == nullptr` 时严格不退化；当传入时仅追加诊断条目，不修改既有诊断顺序。

5.测试：
   - 新增 `tests/unit/backends/abi_calling_convention_test.cpp`：≥ 4 用例覆盖：(a) `ComputeStackFrame` 在零参函数下产出 `total_size == 0` 与空 `saved_regs`；(b) 单 callee-saved 寄存器使用时 `total_size` 16 字节对齐；(c) 7 个 int 参数的 call 触发 `arg_area_size > 0`；(d) `AvailableRegisters()` 返回 `volatile + callee_saved` 的拼接顺序与原 `GetAvailableRegisters()` 字节等价。
   - 新增 `tests/unit/backends/abi_relocation_test.cpp`：≥ 4 用例覆盖：(a) `MapToElfX86_64(kAbs64)==R_X86_64_64==1`、`MapToElfX86_64(kPcRel32)==R_X86_64_PC32==2`、`MapToElfX86_64(kGotPcRel32)==R_X86_64_GOTPCREL==9`、`MapToElfX86_64(kPltPcRel32)==R_X86_64_PLT32==4`；(b) `MapToElfAArch64(kAbs64)==R_AARCH64_ABS64==257`、`MapToElfAArch64(kPage21)==R_AARCH64_ADR_PREL_PG_HI21==275`、`MapToElfAArch64(kPageOff12)==R_AARCH64_ADD_ABS_LO12_NC==277`、`MapToElfAArch64(kBranch26)==R_AARCH64_CALL26==283`；(c) `MapToMachOX86_64(kPcRel32)` 返回 `X86_64_RELOC_BRANCH==2`；(d) 不可映射组合 (例如 `MapToElfX86_64(kBranch26)`) 返回哨兵 `0xFFFFFFFFu` 且 `ToString(kBranch26)=="kBranch26"`、`ParseRelocationKind("kBranch26")==kBranch26`。
   - 新增 `tests/unit/backends/abi_verifier_test.cpp`：≥ 3 用例覆盖：(a) 不传 `AbiContract` 时既有 5 个 verifier 测试结果完全不变（用一个等价合法函数 + 一个缺 terminator 的非法函数双向验证）；(b) 传入 X86_64 `AbiContract` 时，包含 `kCall` + 100 个操作数的函数触发 `kAbiCallArityExceeded`；(c) 传入 Arm64 `AbiContract` 时，正常 4 参 `kCall` 不触发任何诊断。
   - 既有 `test_backends` / `test_core` / `test_middle` / `test_runtime` / `test_linker` 全部不得回归。

6.文档（中英双语）：
   - 新增 `docs/realization/abi.md` 与 `docs/realization/abi_zh.md`，覆盖：`common::abi::CallingConvention` 设计与 `TargetTraits` 协议扩展（4 张静态表 + 3 个 constexpr 常量）、`StackFrame` 字段语义、`ComputeStackFrame` 算法逐步说明、`RelocationKind` 全集与各目标 ELF/Mach-O 整数码映射表、占位 `abi.h` / `relocation.h` 转发别名机制与向后兼容承诺、`AbiContract` 与 verifier 规则 (d)/(e) 的语义、新增后端如何接入 ABI 模板、故障排查（不可映射重定位、verifier 误报、栈帧未对齐）。
   - 更新 `docs/specs/namespace_architecture.md` / `_zh.md`：在 `polyglot::backends::common::machine_ir` 行之后追加 `polyglot::backends::common::abi` 子命名空间行，列出 `CallingConvention` / `StackFrame` / `RelocationKind` / `RelocationEntry` / `AbiDescriptor` / `MapToElfX86_64` / `MapToElfAArch64` / `MapToMachOX86_64` / `MapToMachOArm64` / `ToString` / `ParseRelocationKind` 共 11 个公共符号。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：在既有 §7.11 "Common MachineIR & Verifier" 之后新增 §7.12 "Common ABI & Relocation"，给出 `CallingConvention<TargetTraits>` 接入示例、`RelocationKind` 全表、4 个目标映射函数签名、`AbiDescriptor` 字段表。

7.约束：
   - 不允许最小实现 / 占位 / 空函数体；
   - C++ 代码注释一律英文；
   - 公共类型一律 `polyglot::backends::common::abi`，目标层 `using` 别名置于 `polyglot::backends::<target>`，文件命名小写下划线；
   - 现有 `backends/common/include/abi.h` 与 `backends/common/include/relocation.h` 必须保留为转发头，禁止删除文件；
   - 算法上提后的 `ComputeStackFrame` 必须与原 x86_64 / arm64 版本对相同输入产出字节等价的 `StackFrame`（回归测试保护）；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅做内部结构重构 + 新增 ABI/Reloc 公共接口（向后兼容），版本号 `1.3.4 → 1.3.5`（patch 级）。

--end -done

2026-04-28-2d

背景：从伞形 2026-04-28-2 中拆出，对应原 §4。基线审查显示 `backends/wasm/src/wasm_target.cpp` 单文件已膨胀到 1047 行，按 `// =====` 分为 7 个明显段：(1) 二进制格式常量 ~110 行（`kWasmMagic` / `kWasmVersion` / 60+ `kOp*` 操作码 / `kBlockTypeVoid`），(2) LEB128 编码 ~50 行（`EmitU32Leb128` / `EmitI32Leb128` / `EmitI64Leb128` / `EmitString` / `EmitSection`），(3) IR→Wasm 类型映射 ~25 行（`IRTypeToWasm`），(4) 7 个段发射器 ~95 行（type / import / function / memory / global / export / code），(5) `LowerInstruction` ~325 行（覆盖 binary / cmp / call / load / store / cast / alloca / br / br_if / unsupported），(6) `LowerFunction` ~70 行（locals 压缩 + 块深度建表 + block 包裹），(7) `EmitWasmBinary` 公共入口 ~115 行 + (8) `EmitAssembly` + `EmitInstructionWAT` WAT 文本发射器 ~145 行。该文件违反单一职责，任何下游修改（例如 2026-04-28-2c 的 ABI 表 / 后续 RISC-V smoke / debug emitter 整合）都要在 1k+ 行的 .cpp 中扫描，回归风险与 review 成本都偏高。本子需求把 `wasm_target.cpp` 按上述自然边界切分为 1 + 6 个聚焦 TU，保留 `wasm_target.cpp`（禁止 `git rm`）作为 `EmitWasmBinary` 的公共入口 TU，新增 6 个 .cpp 与 1 个内部常量头，所有方法签名、字节级输出与诊断顺序保持不变（由既有 `wasm_target_test.cpp` 8 个用例 + 新增 smoke 测试守护）。

1.内部常量头：
   - 新增 `backends/wasm/include/internal/wasm_constants.h`：把 `kWasmMagic` / `kWasmVersion` / 全部 `kOp*` 操作码 / `kBlockTypeVoid` 一律改为 `inline constexpr` 形式（C++17），归属命名空间 `polyglot::backends::wasm::internal`。该头是后端实现私有头（不进入 `include/` 公共面），消费者仅限 `backends/wasm/src/**`。
   - 删除 `wasm_target.cpp` 内所有 `[[maybe_unused]] static constexpr` 单值定义（迁移到上述头），但 `wasm_target.cpp` 文件本身保留。

2.编码与段写入 TU：
   - 新增 `backends/wasm/src/encoding/leb128.cpp`：`WasmTarget::EmitU32Leb128` / `EmitI32Leb128` / `EmitI64Leb128` / `EmitString` / `EmitSection` 的字节等价实现迁出。

3.类型映射 TU：
   - 新增 `backends/wasm/src/lowering/type_mapping.cpp`：`WasmTarget::IRTypeToWasm` 的字节等价实现迁出。

4.段发射器 TU：
   - 新增 `backends/wasm/src/sections/section_emitters.cpp`：`EmitTypeSection` / `EmitImportSection` / `EmitFunctionSection` / `EmitMemorySection` / `EmitGlobalSection` / `EmitExportSection` / `EmitCodeSection` 共 7 个方法的字节等价实现迁出。

5.指令降级 TU：
   - 新增 `backends/wasm/src/lowering/instruction_lowerer.cpp`：`WasmTarget::LowerInstruction`（含 binary / return / call / load / store / cast / alloca / unreachable / branch / cond_branch / unsupported 共 11 个分支）字节等价迁出。`lowering_errors_` 写入顺序、`func_name_to_index_` 查找路径、`block_depth_map_` 解析路径全部保持不变。

6.函数降级 TU：
   - 新增 `backends/wasm/src/lowering/function_lowerer.cpp`：`WasmTarget::LowerFunction`（locals 压缩 + 块深度建表 + WASM block 包裹）字节等价迁出。

7.WAT 文本发射器 TU：
   - 新增 `backends/wasm/src/wat_printer.cpp`：`WasmTarget::EmitAssembly` 与 `WasmTarget::EmitInstructionWAT` 字节等价迁出，原匿名命名空间内的 `EmitInstructionWATImpl` 自由函数同步迁入并保留为该 TU 的内部链接（`namespace { … }`），不暴露给其他 TU。

8.公共入口 TU 收敛：
   - `backends/wasm/src/wasm_target.cpp` 改造后只保留 `WasmTarget::EmitWasmBinary` 一个方法（约 115 LOC），include 集合相应收敛到 `<algorithm>` / `<cstdint>` / `<iostream>` / `<vector>` 与必要 IR 头；文件顶部 doxygen 头保留，禁止保留任何注释形式的死代码或被迁出方法的空函数体。

9.CMake 接线：
   - `backends/CMakeLists.txt` 的 `backend_wasm` 目标新增上述 6 个 .cpp 源文件（`encoding/leb128.cpp` / `lowering/type_mapping.cpp` / `sections/section_emitters.cpp` / `lowering/instruction_lowerer.cpp` / `lowering/function_lowerer.cpp` / `wat_printer.cpp`），保留既有 `wasm_target.cpp` / `wasm_target_backend.cpp`；新增 `target_include_directories(backend_wasm PRIVATE backends/wasm/include/internal)`（已隐含在 `${CMAKE_SOURCE_DIR}` 中，但显式声明便于审阅）。

10.测试：
   - 新增 `tests/unit/backends/wasm_split_smoke_test.cpp`：≥ 4 用例覆盖：(a) 空 IR module 调 `EmitWasmBinary` 返回 8 字节 magic+version 头；(b) 单函数 `add(i32,i32)->i32` 模块 `EmitWasmBinary` 输出包含 type/function/memory/export/code 五段且 magic 正确；(c) 同一 IR 模块 `EmitAssembly` 输出 `(module` 起始且包含 `(func $add`；(d) `EmitU32Leb128(0x80)` 编码为 `{0x80, 0x01}`、`EmitI32Leb128(-1)` 编码为 `{0x7F}`（验证编码 TU 拆出后行为不变）。
   - 既有 `tests/unit/backends/wasm_target_test.cpp` 8 个用例不得回归，作为字节等价的护栏。
   - 既有 `test_backends` / `test_core` / `test_middle` / `test_runtime` / `test_linker` 全部不得回归。

11.文档（中英双语）：
   - 新增 `docs/realization/wasm_backend.md` 与 `docs/realization/wasm_backend_zh.md`，覆盖：拆分前的 1047 行单体痛点、新的 6+1 TU 边界与各自职责、`internal/wasm_constants.h` 的可见性策略、`LowerInstruction` 11 个分支的语义清单、`LowerFunction` 块深度建表算法、`EmitWasmBinary` 三阶段（类型收集 / 函数体降级 / 段汇编）流水、`EmitAssembly` WAT 输出 schema、新增 WASM 后端贡献者的接入路径、字节等价回归保护方法。
   - 更新 `docs/specs/namespace_architecture.md` / `_zh.md`：在 `polyglot::backends::wasm` 行追加 `internal::wasm_constants` 子命名空间说明（仅一行注脚，不展开常量清单）。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：在既有 §7.11 之后新增 §7.12 "WASM Backend"，给出 `WasmTarget` 公共方法清单（`EmitWasmBinary` / `EmitAssembly` / `SetModule` / `TargetTriple`）与 6+1 TU 路径表，标注 `internal::` 命名空间为后端私有。

12.约束：
   - 不允许最小实现 / 占位 / 空函数体；
   - C++ 代码注释一律英文；
   - 公共类型一律 `polyglot::backends::wasm`，常量私有头放 `polyglot::backends::wasm::internal`，文件命名小写下划线；
   - `wasm_target.cpp` 不得 `git rm`，必须保留为 `EmitWasmBinary` 的入口 TU；
   - 拆分必须字节等价：`EmitWasmBinary` / `EmitAssembly` 对相同 IR 输入产出与拆分前完全一致的 byte sequence / 文本（既有 8 个 wasm_target_test 用例护栏）；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅做后端内部 TU 拆分（向后兼容、零行为变化），版本号 `1.3.5 → 1.3.6`（patch 级）。

--end -done

2026-04-28-2e

背景：从伞形 2026-04-28-2 中拆出，对应 2a 在 `target_backend.h` / `target_backend.cpp` 中预留的 `EmitBitcode`（2a 末尾明确标注 "默认返回 unsupported 诊断，留待 2026-04-28-2e 启用"）。当前默认实现把所有 `EmitKind::kBitcode` 请求拒之门外，三个后端的 `BackendCapabilities::emits_bitcode` 全部为 `false`，`tools/polyld` 端虽已能识别 `kLLVMBitcode` 魔数但上游没有任何在制品；同时 `middle/src/lto/link_time_optimizer.cpp` 已实现一份基于 `LTOModule::SaveBitcode/LoadBitcode` 的 polyglot bitcode 文本格式（落盘式），LTO/ThinLTO 流水线已经依赖它。本子需求把这条已有的序列化能力上提为内存 API 并接入后端，使 `EmitBitcode` 成为真正可用的发射路径。

1.LTOModule 内存级 bitcode API：
   - 在 `middle/include/lto/link_time_optimizer.h` 的 `LTOModule` 中新增：
     - `std::string SerializeBitcode() const;` —— 返回与现有 `SaveBitcode` 完全一致的字节流（不写盘）；
     - `bool DeserializeBitcode(std::string_view bytes);` —— 从内存字节流恢复，与 `LoadBitcode` 字节等价；
     - `static LTOModule FromIRContext(const polyglot::ir::IRContext& ctx, std::string module_name);` —— 把 `IRContext::Functions()` / `Globals()` 拷贝进 `LTOModule`，把每个 function 视作潜在 entry_point（无属性时全部纳入），保留 block / instruction 拓扑。
   - 现有 `SaveBitcode` 用 `SerializeBitcode` + `std::ofstream` 实现；`LoadBitcode` 用 `std::ifstream::read` + `DeserializeBitcode` 实现；序列化语义保持 100% 兼容（既有 `tests/unit/middle/lto_test.cpp` 全部 SaveBitcode/LoadBitcode 用例必须仍然通过）。

2.`ITargetBackend::EmitBitcode` 默认实现切换：
   - 在 `backends/common/src/target_backend.cpp` 中：原 unsupported 诊断分支删除，改为 `LTOModule::FromIRContext` → `SerializeBitcode()` → 把 UTF-8 字节填入 `result.artifacts.bitcode_bytes`，`result.ok = true`，无诊断；
   - 头文件 `backends/common/include/target_backend.h` 中 `EmitBitcode` 的 doxygen 注释更新：从 "Default implementation reports an unsupported diagnostic" 改为说明默认实现通过 polyglot bitcode（LTO 同源序列化格式）发射，后端可重写以输出 LLVM bitcode；
   - `EmitKind::kBitcode` 的注释保持 "may be unsupported" 措辞不变（针对未来 LLVM bitcode 重写场景）。

3.三个后端能力位翻转：
   - `backends/x86_64/src/x86_target_backend.cpp`、`backends/arm64/src/arm64_target_backend.cpp`、`backends/wasm/src/wasm_target_backend.cpp` 三个 adapter 的 `Capabilities()` 中 `emits_bitcode` 由 `false` 改为 `true`；
   - 不引入新的虚函数重写：三者继续走基类默认 `EmitBitcode`（即第 2 步的 polyglot bitcode 路径）。

4.测试更新：
   - `tests/unit/backends/target_backend_registry_test.cpp` 中：
     - 第 134 行 `REQUIRE_FALSE(info.capabilities.emits_bitcode)` 改为 `REQUIRE(info.capabilities.emits_bitcode)`；
     - 第 197 行 `TEST_CASE("ITargetBackend bitcode default returns unsupported diagnostic", ...)` 整体改写为 `"ITargetBackend bitcode default emits polyglot bitcode bytes"`：构造一个含两个函数的 `IRContext`，调用 `EmitBitcode`，断言 `result.ok == true`、`bitcode_bytes` 非空、首字节为 `'m'`（"module " 前缀）、且 `DeserializeBitcode` 回放得到的 `LTOModule.functions.size() == 2`；
   - 新增 `tests/unit/backends/emit_bitcode_roundtrip_test.cpp`（≥ 3 个 case）：
     - empty IRContext 走通；
     - 三函数 IRContext：每条 backend triple 各序列化一次，断言三份产物字节等价（默认实现共享路径）；
     - block + instruction 拓扑保持：构造一个含 entry block 与 ret 指令的 function，反序列化后断言 block 名 / 指令算子 ≥ 1。

5.CMake 接线：
   - `tests/CMakeLists.txt` 的 `UNIT_TEST_BACKENDS` 列表追加 `unit/backends/emit_bitcode_roundtrip_test.cpp`；
   - 由于默认 `EmitBitcode` 现在依赖 `LTOModule`，`backends/common` 的 `add_library(backend_common ...)` 必须 `target_link_libraries` 链上 `middle_ir`（已存在的 LTO 实现所在 target）。如果已经间接依赖则保持不变，否则在 `backends/CMakeLists.txt` 中追加显式依赖。

6.文档：
   - 新增 `docs/realization/bitcode_emission.md` 与 `_zh.md`：交代 polyglot bitcode 的格式（与 LTO 同源的文本流：`module <name>` 头 → `<fn_count> <gv_count>` → 每函数 `<name>` / `<block_count>` / 每块 `<bname> <inst_count>` / 每指令 `<name> <type_kind> <op_count> <ops...>` → 全局 → entry_points），`EmitBitcode` 三阶段（FromIRContext → Serialize → bitcode_bytes），与 LLVM bitcode 的差异说明（不是 LLVM bc 格式，未来若需 LLVM 互通需要后端覆盖默认实现），以及 polyld 端的消费路径（`linker.cpp` 已通过魔数识别 `kLLVMBitcode`，本格式以 `m` 开头不会被误识别，互不冲突）。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：在 §7.10.2 `TargetOptions/TargetArtifacts` 中给 `bitcode_bytes` 字段加一行说明；在 §7.10 末尾或 §7.12 之后新增 §7.13 "Bitcode Emission"，列出 `LTOModule::SerializeBitcode/DeserializeBitcode/FromIRContext` 与 `ITargetBackend::EmitBitcode` 的契约（成功时 ok/diagnostics/bytes 三元组语义）。
   - 更新 `docs/realization/wasm_backend.md` / `_zh.md`：在能力表行追加一句"默认 EmitBitcode 走 polyglot bitcode 路径"（仅一行注脚，不展开格式细节）。

7.约束：
   - 不允许最小实现 / 占位 / 空函数体；
   - C++ 代码注释一律英文；
   - 不允许 `git rm` 既有文件；不允许动 `LTOModule::SaveBitcode/LoadBitcode` 的对外签名（只增加新方法 + 让旧方法薄壳化）；
   - `EmitKind::kBitcode` 的语义对外不变：调用方仍按 `Capabilities().emits_bitcode` 决策；
   - 现有 `tests/unit/middle/lto_test.cpp` 全部用例必须保持绿色（SaveBitcode/LoadBitcode 字节等价护栏）；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅启用既有但未接通的能力 + 翻转三个 capability flag + 新增 3 个测试用例（向后兼容、无对外接口破坏），版本号 `1.3.6 → 1.3.7`（patch 级）。

--end -done

2026-04-28-2g

MC，本条目为伞形需求 `2026-04-28-2` 的收口子需求（2f RISC-V 子项延后到后续次版本，本伞形以 2a–2e + 2g 完成）。目标：把 `backends/common` 调试信息发射子系统中遗留的所有 `// Placeholder` / `// (placeholder)` / `// Simplified` / `// simplified` 注释及其对应代码做规范化清理，并在不破坏既有 5 个测试套件（test_backends / test_core / test_middle / test_runtime / test_linker）绿色护栏的前提下补齐围绕"长度前缀-后回填" DWARF/ELF 惯用模式的语义注释、把 `EncodeLineStatements` 中以 `address++` 占位的 PC 推进改造成显式按 `DebugLineInfo::address` 单调递增的真实地址跟踪、把 PDB GUID 生成路径中的"simplified"标签替换为符合 RFC 4122 v4 的实现说明，并新增一个面向单元测试粒度的 `debug_emitter_normalization_test.cpp` 守住关键不变量。

具体改造范围：

1.注释规范化（共 12 处，零行为改动）：

   `backends/common/src/debug_emitter.cpp`：
   - 第 102 行 `write_u64(0); // e_shoff (placeholder)` 改为 `write_u64(0); // e_shoff: reserved 64-bit field, patched after section header table is appended (see e_shoff_pos write-back below)`；
   - 第 520 行 `WriteLE<uint32_t>(result, 0); // Placeholder` 改为 `WriteLE<uint32_t>(result, 0); // unit_length: reserved DWARF length prefix, patched via Patch32(unit_length_pos, ...) at end of compilation unit`；
   - 第 827 行 `WriteLE<uint32_t>(result, 0); // Placeholder` 改为 `WriteLE<uint32_t>(result, 0); // unit_length: reserved DWARF length prefix for .debug_line, patched after line program is fully encoded`；
   - 第 840 行 `WriteLE<uint32_t>(result, 0); // Placeholder` 改为 `WriteLE<uint32_t>(result, 0); // header_length: reserved DWARF length prefix for line header, patched after file/directory tables are written`；
   - 第 866 行 `WriteLE<uint32_t>(result, 0); // Placeholder for length` 改为 `WriteLE<uint32_t>(result, 0); // CIE length: reserved, patched after CIE body and padding are appended (DWARF v5 §6.4.1)`；
   - 第 915 行 `WriteLE<uint32_t>(result, 0); // Placeholder for length` 改为 `WriteLE<uint32_t>(result, 0); // FDE length: reserved, patched after FDE body and padding are appended (DWARF v5 §6.4.1)`；
   - 第 966 行 `WriteLE<uint32_t>(result, 0); // placeholder for length` 改为 `WriteLE<uint32_t>(result, 0); // .eh_frame CIE length: reserved, patched after CIE body and padding (System V ABI §10.6.1)`；
   - 第 1019 行 `WriteLE<uint32_t>(result, 0); // placeholder for length` 改为 `WriteLE<uint32_t>(result, 0); // .eh_frame FDE length: reserved, patched after FDE body and padding (System V ABI §10.6.1)`；
   - 第 1074 行 `// Placeholder` 同类替换为对应 .debug_aranges / .debug_pubnames 段的"reserved length, patched at section close"语义注释；
   - 第 1135 行 `// GUID generation (simplified)` 改为 `// PDB GUID generation: RFC 4122 v4 (random) compliant; uses std::random_device for entropy and explicitly sets version (4) and variant (1) bit-fields per spec`；

   `backends/common/src/dwarf_builder.cpp`：
   - 第 269 行 `// Line number program (simplified)` 改为 `// Line number program: emits one row per source-line entry using DW_LNE_set_address + DW_LNS_set_file + DW_LNS_advance_line + DW_LNS_copy; complex special-opcode encoding is not used because the legacy DwarfBuilder path is reserved for fallback; production path lives in DwarfSectionBuilder::EncodeLineStatements`。

2.行为规范化（仅 1 处真实代码改动）：

   `backends/common/src/debug_emitter.cpp::DwarfSectionBuilder::EncodeLineStatements`（约第 753–812 行）：
   - 删除第 808 行 `address++; // Simplified address tracking`；
   - 在循环开始处把 `uint64_t address = 0;` 的语义改为"已发射到 DWARF 状态机的最后一个 PC"，并在每个 `entry` 处理时根据 `entry.address` 与 `address` 的差值采用 DWARF 标准做法推进：若 `delta > 0` 则发射 `DW_LNS_advance_pc + ULEB128(delta)`；若 `delta == 0` 则不发射 PC 推进；若 `delta < 0`（异常，非单调）则降级为重新 `DW_LNE_set_address` 一次（这种情况不应在合法 IR 中出现，仍要保持鲁棒）；
   - 第一次 `entry`（`address == 0`）继续走 `DW_LNE_set_address(0)` + 后续标准推进，不再无条件 `address++`；
   - 在 `kDwLnsCopy` 后赋值 `address = entry.address;`，确保状态机寄存器与真实 PC 一致；
   - `kDwLnsAdvancePc` 由原本 `[[maybe_unused]]` 改为正式启用（删除 attribute）。

3.测试新增：

   `tests/unit/backends/debug_emitter_normalization_test.cpp`（≥ 4 个 case，仅依赖既有 `DebugInfoBuilder` / `DwarfSectionBuilder` / `PdbSectionBuilder` 公开 API，不调用任何外部 `dwarfdump` / `llvm-pdbutil`）：
   - case 1 `.debug_info unit_length is reserved-then-patched`：构造一个含 1 个 compile unit + 1 个 subprogram 的 `DebugInfoBuilder`，调用 `BuildDebugInfo`，断言返回 `bytes.size() >= 11` 且 `LE32(bytes, 0) == bytes.size() - 4`；
   - case 2 `.debug_line unit_length and header_length are reserved-then-patched`：同上方法构造一份带 2 行的行号信息，断言 `LE32(bytes, 0) == bytes.size() - 4`，且头部 `header_length` 字段（位于 unit_length(4) + version(2) + address_size(1) + segment_selector_size(1) 之后的 4 字节）非零并 < `bytes.size()`；
   - case 3 `.debug_line line program advances PC monotonically`：构造 3 行同文件不同 address 的 `DebugLineInfo`（address 单调递增），断言序列化结果中至少出现一次 `DW_LNS_advance_pc`(0x02) 字节；
   - case 4 `PDB GUID has RFC 4122 v4 + variant 1 bits`：调用 `PdbSectionBuilder::BuildPdbInfoStream` 取末 16 字节，断言 `(guid[6] & 0xF0) == 0x40 && (guid[8] & 0xC0) == 0x80`；并断言两次连续调用 GUID 不全相同（熵存在）。

4.CMake 接线：

   - `tests/CMakeLists.txt` 的 `UNIT_TEST_BACKENDS` 列表追加 `unit/backends/debug_emitter_normalization_test.cpp`；
   - 不新增任何依赖（沿用既有 `backend_common` 链路）。

5.文档：

   - 新增 `docs/realization/debug_emitter_normalization.md` 与 `_zh.md`：
     - 解释 DWARF/ELF "reserved length-prefix + patch-at-close" 惯用模式（为什么写 0 不是占位 bug）；
     - 说明 `EncodeLineStatements` PC 推进的新语义（按 `DebugLineInfo::address` 真实推进，使用 `DW_LNS_advance_pc`）；
     - 列出 PDB GUID 的 RFC 4122 v4 合规细节；
     - 给出新增 4 个单元 case 的不变量清单。
   - 更新 `docs/api/api_reference.md` / `_zh.md`：在调试发射相关节（`DwarfSectionBuilder` / `PdbSectionBuilder`）末尾追加一节"Reserved-length encoding contract"，说明所有 `// reserved ... patched` 注释代表的不变量。
   - 不引入新的对外公开 API；如有头文件 `debug_emitter.h` 中需要标注的 doxygen 调整，仅做注释级修补。

6.约束：

   - 不允许最小实现 / 占位 / 空函数体；本子需求自身不引入任何新的占位注释；
   - C++ 代码注释一律英文；
   - 不允许 `git rm` 既有文件；不允许改 `DwarfSectionBuilder` / `DebugInfoBuilder` / `PdbSectionBuilder` 的对外公开签名（只允许内部行为细化 + 注释规范化 + 新增私有助手）；
   - 既有 5 个测试套件（test_backends / test_core / test_middle / test_runtime / test_linker）必须全部保持绿色；test_backends 期望 case 数 +4、断言数随之增长；
   - 行为改动仅限第 2 条（PC 推进），其它 11 处为纯注释级；
   - 文档须中英双语两份；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因本条目同时承担伞形需求 `2026-04-28-2` 的收口（2a–2e + 2g 已闭环，2f RISC-V 子项延后），版本号按伞形原计划做次版本跃迁：`1.3.7 → 1.4.0`；伞形条目自身不再单独追加 `--end -done`（按 2026-04-28-2 末尾规约："当 2a–2g 全部追加 --end -done 时视作伞形完成"，2f 缺席视为延后释出，不阻塞伞形收口）。

--end -done

2026-04-28-3

MC，本条目专注文档侧整理：将更新日志从 `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md` 第 14.6 节剥离，独立成顶层 `docs/CHANGELOG.md` + `docs/CHANGELOG_zh.md` 双语文档，并补齐 1.3.2 → 1.4.0 区间所有版本（即新近完成的后端注册表、MachineIR 校验器、ABI/Reloc 重写、WASM TU 拆分、EmitBitcode 启用、调试发射规范化六个版本）的条目；同步更新 `README.md` 头部测试徽章、版本脚注、Last Updated 日期、文档目录链接，以及 USER_GUIDE 双语版的版本号 / 日期 / TOC 结构。

具体改造范围：

1.新增 `docs/CHANGELOG.md` 与 `docs/CHANGELOG_zh.md`：
   - 顶部加项目名 / 维护说明 / 版本范围（v0.1.0 → v1.4.0）/ 与 USER_GUIDE 的相互引用；
   - 按版本号倒序列出全部既有条目（直接搬运 USER_GUIDE 第 14.6 节内容，不丢任何字段）；
   - 在最顶部追加 6 个新条目，时间统一标 `2026-04-28`：
     - **v1.4.0 (2026-04-28)** — 调试发射规范化 + 伞形收口；
     - **v1.3.7 (2026-04-28)** — `ITargetBackend::EmitBitcode` 默认实现接通 polyglot bitcode 发射，三个 adapter 翻转 `emits_bitcode = true`；
     - **v1.3.6 (2026-04-28)** — WASM 后端 TU 拆分（`wasm_target.cpp` 1500+ 行 → 多文件按模块/类型/指令/runtime 域职责分解）；
     - **v1.3.5 (2026-04-28)** — ABI / 重定位模型重写（统一 `RelocationKind` + `ABIDescriptor`，三平台 e2e 一致）；
     - **v1.3.4 (2026-04-28)** — MachineIR 验证器（前缀寄存器使用、栈帧封闭性、跨基本块寿命检查）；
     - **v1.3.3 (2026-04-28)** — 后端注册表（`ITargetBackend` + `BackendRegistry`，三平台 adapter）；
   - 每条新条目 4–8 个 `- ✅` 项，覆盖：核心改动、新增/移动文件、测试增量、对外接口边界、向后兼容性。
2.从 `docs/USER_GUIDE.md` 中删除 `## 14.6 Changelog` 整节（含其下所有 `### vX.Y.Z` 版本子节）；在原位置插入一段不超过 6 行的指针小节，把读者引向新独立的 `docs/CHANGELOG.md`，附中英文双语链接。`docs/USER_GUIDE_zh.md` 同步处理（删除 `## 14.6 更新日志` 整节，插入同等指针小节，链接指向 `docs/CHANGELOG_zh.md`）。
3.同步 USER_GUIDE 双语头部 / 尾部元信息：
   - 头部 `**Version**: v1.1.1` / `**Last Updated**: 2026-04-27` 改为 `v1.2.0` / `2026-04-28`（中文版同步）；
   - 尾部 `<!-- BEGIN:version_footer_en -->` / `<!-- BEGIN:version_footer_zh -->` 块内 `Document Version: v1.1.1` / `Last Updated: 2026-04-27` 改为 `v1.2.0` / `2026-04-28`；
   - TOC（章节目录）中"Appendix"条目保留不变，§14 子节列表里"14.6 Changelog"已不复存在，无需在 TOC 里新增条目（指针小节是普通文本段，不要给四级标题）。
4.更新 `README.md`：
   - 测试徽章 `1084_cases_|_3_suites` 改为更准确的当前测试套件描述：保持徽章风格，改为 `Suites-5%20core%20%2B%208%20frontend-brightgreen`（即 5 套核心套件 + 8 套前端套件）；不在徽章里硬编码 case 数（原 1084 早已过时）；
   - "Documentation / 文档"小节追加 `[CHANGELOG.md](docs/CHANGELOG.md)` / `[CHANGELOG_zh.md](docs/CHANGELOG_zh.md)` 行，紧接 USER_GUIDE 行之后；
   - 尾部 `<!-- BEGIN:version_footer_en -->` 块内 `Document Version: v1.1.1` / `Last Updated: 2026-04-27` 改为 `v1.2.0` / `2026-04-28`；
   - "Test Statistics"与"Unit Test Breakdown"两小节及其陈述的 1019 / 1084 / 909 数字保留，但在标题下加一行注脚：表内数字反映 2026-03-19 拆分前的统计快照，最新分套件断言 / 用例数请以独立 `CHANGELOG.md` 中各版本条目为准；
   - 不动架构图、quick-start、.ploy 例子、Plugin 列表等正文。
5.约束：
   - 不引入任何新的代码改动；不动 CMake 版本号（仍为 `1.4.0`）；
   - 不允许最小实现 / 占位 / 空小节；
   - 文档中文 / 英文成对存在；
   - CHANGELOG 内不出现"demand"字样（规则 10）；
   - 既有 5 个测试套件不受影响（无需重新构建 / 跑测；本条目纯文档）；
   - 完成后在本条目末尾追加 `--end -done`；
   - 因仅文档调整（无代码 / 无 ABI / 无构建参数变化），CMake 项目版本不变；USER_GUIDE 文档版本独立递进 `v1.1.1 → v1.2.0`（结构性章节剥离，文档语义级 minor），README 文档脚注同步对齐。

--end -done

2026-04-28-03

sample无法编译，请帮我修复
```bash
(base) PS D:\Others\PolyglotCompiler> .\build\polyc.exe .\tests\samples\03_pipeline\pipeline.ploy
========================================
 PolyglotCompiler v1.4.0  (polyc)
========================================
[polyc] Source: D:\Others\PolyglotCompiler\tests\samples\03_pipeline\pipeline.ploy
[polyc] Language: ploy (auto-detected)
[polyc] Arch: x86_64
[polyc] Opt: O0
[polyc] Output: a.out
[polyc] Mode: permissive
----------------------------------------
[polyc] Staged compilation pipeline (.ploy)... [pipeline] link descriptors -> D:\Others\PolyglotCompiler\tests\samples\03_pipeline\aux\pipeline_link_descriptors.paux
'link' 不是内部或外部命令，也不是可运行的程序
或批处理文件。
'lld-link' 不是内部或外部命令，也不是可运行的程序
或批处理文件。
done (74.8ms)
  - frontend: 5.1ms
  - semantic-db: 2.4ms
  - marshal-plan: 0.0ms
  - bridge-generation: 0.7ms
  - backend: 11.9ms
  - packaging: 53.3ms
[polyc] Compilation successful (staged pipeline).
```

--end -done

2026-04-28-4

请大幅丰富 `tests/samples/` 下的示例内容，要求覆盖更广、更真实、更有教学价值，具体如下：

1. 新增至少 14 个示例文件夹（编号从 `17_` 起连续递增），每个示例必须包含：
   - 一个 `.ploy` 入口文件；
   - 至少两种宿主语言的真实可编译源文件（C++/Python/Rust/Java/C# 中至少 2 种，鼓励 3 种及以上）；
   - 一个 `README.md`（中英双语，分两个文件 `README.md` / `README_zh.md`），说明示例目标、涉及的 ploy 关键字、运行方式与预期输出；
   - 一个 `expected_output.txt`，记录正确执行后的标准输出（用于回归比对）。

2. 主题建议（必须全部覆盖，可在此基础上扩展）：
   - `17_string_processing/`：跨语言字符串/编码处理（UTF-8 / UTF-16 / 字节流互转）；
   - `18_numeric_kernels/`：跨语言数值内核（C++ SIMD + Python NumPy + Rust 计算）；
   - `19_file_io/`：跨语言文件 I/O（顺序/随机/二进制/文本）；
   - `20_json_pipeline/`：跨语言 JSON 解析-转换-序列化流水线；
   - `21_image_processing/`：跨语言图像处理（C++ 解码 -> Rust 滤镜 -> Python 可视化）；
   - `22_database_access/`：跨语言访问 SQLite（建表、读写、事务）；
   - `23_http_client/`：跨语言 HTTP 客户端（Rust 发请求、Python 解析、C++ 聚合）；
   - `24_concurrency/`：跨语言并发与共享数据（线程池 + 通道 + 锁）；
   - `25_event_loop/`：跨语言事件循环 / 回调注册（Python 注册 -> C++ 触发）；
   - `26_state_machine/`：跨语言状态机（Rust 定义状态、ploy `MATCH` 驱动）；
   - `27_plugin_system/`：跨语言插件系统（C++ 宿主加载 Python/Java 插件）；
   - `28_ml_inference/`：跨语言 ML 推理（Python 模型加载 + C++ 张量预处理 + Java 后处理）；
   - `29_data_analytics/`：跨语言数据分析（CSV -> DataFrame -> 报表）；
   - `30_game_loop_demo/`：跨语言"游戏主循环"（C++ 帧驱动 + Python 脚本逻辑 + Rust 物理）。

3. 同时强化已有 `01_-16_` 示例：
   - 为每个旧示例补齐缺失的 `README_zh.md` 与 `expected_output.txt`；
   - 旧示例中 `.cpp/.py/.rs/.java/.cs` 必须保证单独构建可通过（不得只是片段）。

4. 在 `tests/samples/README.md` 与 `tests/samples/README_zh.md` 中：
   - 更新示例目录表（涵盖新增 14 个）；
   - 新增"按主题索引"小节（字符串 / 数值 / I/O / 网络 / 并发 / 数据 / ML 等）；
   - 新增"按宿主语言组合索引"小节（cpp+py、cpp+rs、py+java …）。

5. 在 `scripts/` 下新增 `build_all_samples.ps1` 与 `build_all_samples.sh`：
   - 遍历 `tests/samples/` 下所有 `*.ploy`，依次调用 `polyc` 编译；
   - 对每个示例运行编译产物，将 stdout 与 `expected_output.txt` 逐行比对；
   - 输出汇总报告（成功 / 失败 / 输出不匹配的清单）。

6. 在 `tests/integration/` 中新增 `samples_regression_test.cpp`：
   - 通过 CTest 调用上述脚本，作为整套示例的集成回归；
   - 加入 `CMakeLists.txt` 现有 integration 套件，不得破坏现有测试。

7. 文档与版本：
   - 更新中英双语 `USER_GUIDE` 与 `README` 中"示例"章节，列出新增示例；
   - 更新 `CHANGELOG`（中英双语，若已有）；
   - 项目版本号在根目录 `CMakeLists.txt` 中按 minor 递进（新增功能型样例与脚本，无 ABI 变更）；
   - 所有新增注释 / 文档不得出现与本条目自身相关的字样（规则 10）；
   - 新增的 `.ploy` 与宿主语言源文件中的注释一律使用英文（规则 2）。

8. 验收标准：
   - `build_all_samples.ps1` 在本机一次性运行，所有示例编译成功且输出与 `expected_output.txt` 完全一致；
   - `ctest -R samples_regression` 通过；
   - 不允许任何"占位 / 最小实现 / TODO"出现在新增样例中（规则 3）；
   - 完成后在本条目末尾追加 `--end -done`。

阶段性进度（v1.42.2）：
- Mach-O writer 修复 5 项：LC cmdsize off-by-pad、arm64 cpusubtype、LC_MAIN entryoff、
  section flags by-segname 分类、section data 与 sect record offset 对齐；产出文件
  `file` 鉴定、`otool -h/-l/-tv`、`codesign -vv` 全部通过。
- 仍需推进：__LINKEDIT 为 code signature 预留容量 + LC_DYSYMTAB 计数与实际符号表
  一致性 + dyld bind opcodes 到 _write/_exit 运行时 + backend Mach-O
  GOT_LOAD_PAGE21/PAGEOFF12 reloc，方使 AMFI/dyld 接受 ad-hoc 签名产物并真运行。
- 样例 OK 桶在本阶段性进度下仍为 0/41；demand-04 未达验收标准 8，不追加 done。

--end

2026-04-28-5

为 PolyglotCompiler IDE（`polyui`）新增"性能分析器（Profiler）"与"调用分析器（Call Analyzer）"两个一等公民面板，要求与 IDE 现有 Topology / Editor / Console 面板风格一致，并与 `polybench` / `polyrt` / `middle` 已有数据通道打通。具体需求：

1. 性能分析器（Performance Profiler）面板：
   - 在 `tools/polyui/` 下新增 `profiler_panel.{h,cpp}`，注册为 IDE 主停靠面板（与 Topology 面板并列），快捷键 `Ctrl+Shift+P`；
   - 数据来源：调用 `polybench --json <out>` 与 `polyrt bench --json <out>`，统一接入 `tools/polyui/profile_session.{h,cpp}`；
   - 必须包含的可视化子视图：
     * 火焰图（Flame Graph，按 inclusive 时间）；
     * 时间线（Timeline，按线程 / 跨语言桥接事件 swimlane 显示，支持缩放、拖拽）；
     * Top-N 热点表（Self / Total / Calls / Avg，可按列排序与正则过滤）；
     * 跨语言开销饼图（C++ / Python / Rust / Java / .NET / bridge 各占比）；
     * 内存分配时间序列（来自 `polyrt gc --json`）；
   - 实时模式：当 `polyrt` 以 `--profile-stream <pipe>` 运行时，IDE 通过命名管道 / 本地 socket 实时刷新（≥ 5 Hz）。

2. 调用分析器（Call Analyzer）面板：
   - 在 `tools/polyui/` 下新增 `call_analyzer_panel.{h,cpp}`，快捷键 `Ctrl+Shift+G`；
   - 数据来源：编译期 `polyc --emit call-graph <out>.cgjson` + 运行期 `polyrt` 计数（已经存在的 g_stats 通道扩展，必要时在 `runtime/` 中新增 `call_trace.{h,c}`，提供 `__ploy_rt_call_enter` / `__ploy_rt_call_exit` 钩子，由 lowering 在 `CALL` / `METHOD` / `NEW` / `LINK stub` 入口插桩）；
   - 必须包含的可视化与交互：
     * 静态调用图（来自 `polyc` AST + LINK 解析），节点按宿主语言着色，边按 marshal 方向标注；
     * 动态调用图叠加层（运行计数 / 平均耗时染色）；
     * "选中函数 -> 双击跳转源码"（联动 Editor 面板，要求 `.ploy` / `.cpp` / `.py` / `.rs` / `.java` / `.cs` 全部可跳转）；
     * 反向调用者 / 正向被调者两侧栏树视图；
     * 跨语言边过滤器（仅 cpp->py、仅 py->java …）；
     * 路径搜索：给定 src/dst 函数，列出全部静态可达路径（DFS，深度上限可配置）。

3. 编译器与运行时支持（不允许只在 IDE 端造数据）：
   - `polyc` 新增 `--emit call-graph <file>` 与 `--emit profile-symbols <file>`，输出稳定 schema 的 JSON（schema 写入 `docs/specs/`，中英双语）；
   - `polyrt` 新增 `polyrt profile`、`polyrt calltrace` 子命令，对齐 `status/gc/thread/bench` 风格，支持 `--json`、`--stream <pipe>`；
   - `runtime/` 中实现 `call_trace.{h,c}`、`profile_sink.{h,c}`，与现有 `gc_api.h`、`threading.h` 风格一致，并在 `CMakeLists.txt` 中纳入 `runtime` 目标；
   - `middle/` 添加可选 pass `InstrumentCallTrace`（受 `--profile-instrument` 开关控制），插桩开销可被 LTO 在未启用时完全消除（dead-code-stripped）。

4. IDE 工程化：
   - 两个面板共用 `tools/polyui/data_models/`（`flame_node.{h,cpp}`、`call_graph_model.{h,cpp}`、`timeline_model.{h,cpp}`），均派生自 `QAbstractItemModel` / `QAbstractListModel`，与现有 topology model 风格一致；
   - 主题：完整接入现有 light/dark 主题，颜色变量统一从 `tools/polyui/themes/` 读取；
   - 配置项：在 IDE Settings 面板新增"Profiler"分组（采样频率、最大火焰深度、是否自动启动 `polyrt --profile-stream`、调用图最大节点数、布局算法 ELK / dagre / force）。

5. 测试：
   - `tests/unit/` 新增 `profile_session_test.cpp`、`call_graph_model_test.cpp`、`call_trace_runtime_test.cpp`；
   - `tests/integration/` 新增 `profiler_e2e_test.cpp`：编译并运行 `tests/samples/09_mixed_pipeline` 与 `tests/samples/15_full_stack`，验证：
     * `polyc --emit call-graph` 产物的节点数 / 边数与静态分析结果一致；
     * `polyrt profile --json` 产物中 hot function 排名稳定（允许 ±1 名次抖动）；
   - 所有用例必须挂入 CTest，不得 `#if 0` 或 `REQUIRE(true)` 占位；
   - 新增 `tests/topology_ui` 风格的 `tests/topology_ui/profiler_panel_smoke_test.cpp`，最小启动 IDE 并校验两个面板可注册、可显示、无 QObject 警告。

6. 文档：
   - `docs/realization/` 新增 `profiler_zh.md` / `profiler_en.md` 与 `call_analyzer_zh.md` / `call_analyzer_en.md`，详尽说明数据通道、JSON schema、扩展点；
   - `docs/api/` 新增 `polyrt_profile_api_zh.md` / `polyrt_profile_api_en.md`、`polyc_call_graph_api_zh.md` / `polyc_call_graph_api_en.md`；
   - `docs/tutorial/` 新增 "性能分析入门" / "Profiling Quickstart" 与 "调用分析入门" / "Call Analyzer Quickstart" 两组中英双语教程，附图；
   - `USER_GUIDE` 中英双语在"IDE 功能"章节新增"Profiler"与"Call Analyzer"两节；
   - `README` 中英双语在"Tooling"小节列出新面板与快捷键。

7. 版本与规约：
   - 根 `CMakeLists.txt` 项目版本按 minor 递进（功能新增、IDE 面板与运行时插桩，无 ABI 破坏）；
   - 所有新增 C/C++/QML 注释一律使用英文（规则 2）；
   - 所有新增文档与注释不得出现与 `demand.md` 自身相关的字样（规则 10）；
   - 不允许任何"占位 / 最小实现 / TODO"出现在本条目交付物中（规则 3）。

8. 验收标准：
   - 在 `tests/samples/09_mixed_pipeline` 上：`polyc --emit call-graph` 一次成功，IDE 中"调用分析器"面板能加载并显示完整跨语言调用图，节点 ≥ 真实函数数；
   - `polyrt profile --stream` + IDE Profiler 实时模式可稳定运行 ≥ 60 秒不丢帧（≥ 5 Hz 刷新）；
   - 全部新增单元 / 集成 / smoke 测试通过；
   - 完成后在本条目末尾追加 `--end -done`。

--end -done

2026-04-28-6

[P0/P1 词法层] 关键字大小写无关、删除冗余关键字、统一逻辑运算符。

1. 词法器（`frontends/ploy/src/lexer/lexer.cpp`）：
   - 关键字识别改为**大小写无关**：`LINK` / `link` / `Link` 均被识别为同一 `kKeyword`；
   - 在 `Token` 中保留原始 lexeme（用于诊断与格式化器还原），但 `kind` 与关键字归一表驱动；
   - 标识符仍区分大小写；新增 lexer 单元测试覆盖混合大小写。

2. 推荐风格：
   - 文档与全部新写示例一律使用**小写**关键字；
   - `tests/samples/` 现有大写示例保留不动（兼容性），新增示例统一小写；
   - IDE 格式化器（若后续接入）默认把关键字归一为小写。

3. 删除冗余 / 废弃：
   - 移除关键字 `RETURNS`（语言中从未实际使用），词法仍接受但 sema 给出 `deprecated` 警告，编译可继续；
   - 逻辑运算符 `AND` / `OR` / `NOT` 与 `&&` / `||` / `!` 之间确立**唯一推荐形式**：
     * 推荐 `&&` / `||` / `!`（与 C/C++/Rust/Python 习惯接近）；
     * 关键字形式保留为 alias，sema 不警告，但文档明示"推荐符号形式"。

4. 文档：
   - 中英双语更新 `docs/specs/language_spec*.md` §2.2 / §2.10、`docs/realization/ploy_language_spec*.md` §3.1；
   - 在 `USER_GUIDE` 中英双语"语法风格"一节加入"关键字大小写无关 + 推荐小写"说明；
   - `CHANGELOG`（中英双语）记录 deprecation 列表。

5. 测试：
   - `tests/unit/frontend_ploy/lexer_case_insensitive_test.cpp`：覆盖 54 关键字混合大小写；
   - `tests/unit/frontend_ploy/keyword_alias_test.cpp`：`AND` ↔ `&&` 解析等价；
   - `tests/integration/`：新增一个全小写示例，确保端到端通过。

6. 版本：根 `CMakeLists.txt` patch 递进（语法兼容增强，无破坏）。
   完成后追加 `--end -done`。规则 2 / 3 / 10 全部生效。

--end -done


2026-04-28-7

[P0/P2 类型基础] 引入显式位宽数值类型 + `TYPE` 别名 + `CONST`。

1. 词法 / 解析：
   - 新增基础类型关键字：`i8` `i16` `i32` `i64` `u8` `u16` `u32` `u64` `f32` `f64` `usize` `isize`；
   - 旧关键字 `INT` / `FLOAT` 作为 alias：`INT` ≡ `i64`，`FLOAT` ≡ `f64`（spec 中明确写明，并给出"为何 INT=i64"的语言学说明）；
   - 新增 `TYPE <name> = <type_expr>;` 类型别名声明；
   - 新增 `CONST <name>: <type> = <const_expr>;` **编译期常量**（必须可在 sema 阶段折叠）。

2. 语义：
   - sema 给出"类型宽度不匹配"细致诊断（参考 2026-02-20-4 的细化报错风格）；
   - `TYPE` 别名在 sema 表中保留原名，错误消息打印 `T (alias of i32)`；
   - `CONST` 引用进入常量传播 pass，与 middle 已有 const-prop 联通。

3. 跨语言映射：
   - 更新 `docs/specs/language_spec*.md` §2.9 表，给出 `i32 / i64 / u32 / u64 / f32 / f64` 与各宿主语言的精确对应（C++ `int32_t` 等、Rust `i32` 等、Java `int`/`long` 等、C# `int`/`long` 等、Python `int`/`numpy.int32`）；
   - `runtime/` marshalling 表对齐位宽。

4. 文档：
   - 中英双语更新所有受影响的 spec / realization / USER_GUIDE / tutorial 的"基础类型"小节；
   - `tests/samples/` 新增一个 `31_explicit_widths/` 示例，演示 `i32 / u64 / f32` 跨语言。

5. 测试：
   - `tests/unit/frontend_ploy/type_alias_test.cpp`、`const_decl_test.cpp`、`width_mismatch_diag_test.cpp`；
   - `tests/integration/`：覆盖位宽匹配 + 误用诊断。

6. 版本：minor 递进（新增类型 + 新关键字，向后兼容）。
   完成后追加 `--end -done`。

--end -done


2026-04-28-8

[P0/P1 语法清理] 统一 `LINK` 形式、提升 `STAGE` 为关键字。

1. `LINK` 形式收敛：
   - **保留并推荐**：`LINK <lang>::<module>::<func> AS FUNC(<types>) -> <ret>;`（带签名版）；
   - **降级**：`LINK(target_lang, source_lang, target_func, source_func);` 旧形式仍可解析，sema 给出 `deprecated` 警告并提示如何改写；
   - 旧形式参数个数依赖 `MAP_TYPE` 推断的兜底逻辑保留至下个 minor 版本，下个 minor 版本起删除（在 `CHANGELOG` 中预告）。

2. `STAGE` 关键字化：
   - 把 `STAGE` 提升为正式关键字（仅在 `PIPELINE` 块内合法，块外仍可作为标识符是不可接受的——一律视为关键字以便 IDE 一致染色）；
   - 词法表 + spec 关键字总数从 54 → 55+（含 P0-7 新增数值类型）；
   - parser 在 `PIPELINE` 之外见到 `STAGE` 报"unexpected keyword `stage` outside PIPELINE"。

3. 文档：
   - 中英双语 spec / realization / USER_GUIDE / tutorial 全部以"带签名"形式重写 `LINK` 示例；
   - 旧示例（`tests/samples/01_basic_linking` 等）若使用旧形式，**新增镜像版本**（`01_basic_linking_v2/`）展示新形式，旧版本保留并在 README 中标注 "legacy form"。

4. 测试：
   - 旧形式 + deprecation diag 单测；
   - 新形式 sema 校验单测；
   - `STAGE` 误用诊断单测。

5. 版本：minor 递进。
   完成后追加 `--end -done`。

--end -done


2026-04-28-9

[P0 类型安全] 跨语言对象进入静态类型系统：`HANDLE<lang::Class>`。

1. 类型新增：
   - 引入不透明句柄类型 `HANDLE<<lang>::<class_path>>`，例如 `HANDLE<python::torch::nn::Linear>`；
   - `NEW(lang, class, args...)` 的返回类型由 `Any` 改为 `HANDLE<lang::class>`；
   - `METHOD(lang, obj: HANDLE<lang::T>, name, args...)` 的参/返回类型由 sema 在已注册的方法签名表中查找；
   - `GET / SET` 同理，参考已注册属性签名。

2. 方法 / 属性签名注册：
   - 新增声明：
     ```ploy
     class python::torch::nn::Linear {
       method forward(input: HANDLE<python::torch::Tensor>) -> HANDLE<python::torch::Tensor>;
       attr in_features: i32;
     }
     ```
     用于显式登记跨语言类的方法签名；未登记时 sema 给"unknown method, falling back to dynamic dispatch"**warning**（不致命，保持向后兼容）。
   - 也支持从 IDE/工具自动生成（后续可由 `polyc --emit class-stubs <lang> <module>` 提供）。

3. 跨语言传参：
   - `HANDLE<a::T>` 不能隐式转 `HANDLE<b::U>`；
   - 显式转换走 `CONVERT(target_handle_type, source_obj)`，由用户提供 `MAP_FUNC`。

4. 兼容性：
   - 旧代码继续可写 `LET m = NEW(python, torch::nn::Linear, 10, 5);`，sema 推断 `m: HANDLE<python::torch::nn::Linear>`；
   - 仅当用户显式标注错误目标类型时才报错。

5. 文档：
   - `docs/realization/cross_language_oop_*.md`（新建中英双语）；
   - `docs/specs/language_spec*.md` §2.7 重写；
   - USER_GUIDE / tutorial 中英双语补"静态类型化对象互操作"章节；
   - 已有限制表删除"静态类型未知"行（与 2026-02-20-4 呼应）。

6. 测试：
   - sema 单测：方法签名命中 / 不命中 / 类型不兼容；
   - 集成：`tests/samples/05_class_instantiation/` 升级带签名版；新增 `32_typed_handles/`。

7. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-10

[P0 模式匹配] 扩展 `MATCH` 模式语义。

1. 支持模式：
   - 字面量（已支持）；
   - 通配 `_`；
   - 范围 `CASE 1..10` / `CASE 1..=10`；
   - 元组解构 `CASE (a, b)` / `CASE (_, b)`；
   - 结构体解构 `CASE Point { x, y }` / `CASE Point { x, .. }`；
   - 类型守卫 `CASE x: i32 IF x > 0`；
   - 多模式 OR：`CASE 1 | 2 | 3`；
   - 绑定：`CASE n @ 0..100`；
   - `OPTION` 解构：`CASE Some(x)` / `CASE None`。

2. 详尽匹配检查（exhaustiveness）：
   - sema 进行覆盖性检查，遗漏报错，建议自动补 `_`；
   - 不可达分支报警告。

3. 解析 / AST：
   - `parser.cpp` 新增 `ParsePattern` 子模块；
   - `ploy_ast.h` 新增 `PatternNode` 系列。

4. 文档：
   - 中英双语 spec / realization / USER_GUIDE / tutorial 同步更新 `MATCH` 章节；
   - `tests/samples/` 新增 `33_pattern_matching/`，覆盖全部模式形态。

5. 测试：
   - sema 详尽性 / 不可达性 / 嵌套模式；
   - lowering 分支表生成验证。

6. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-11

[P1 一致性] 命名实参默认值、`AS` 语义集中说明、收紧 `EXTEND`。

1. 命名实参默认值：
   - 在 `FUNC f(x: i32, y: i32 = 0) -> i32 { … }` 中支持默认值；
   - 调用点：`f(x: 1)` 等价于 `f(1, 0)`；`f(1, y: 5)` 合法；位置参数后不可再出现位置参数。
   - sema 校验默认值表达式必须是 `CONST` 可折叠或纯函数调用。

2. `AS` 语义集中说明：
   - `AS` 当前用于：① IMPORT 别名；② EXPORT 别名；③ LINK 签名分隔；④ PACKAGE 别名；⑤ CONVERT 目标类型。
   - 在 spec 中**新增专章**集中列出，并给出反例（哪些写法歧义、被禁）。
   - IDE 自动补全提示需基于上下文给出最相关一种。

3. 收紧 `EXTEND`：
   - 静态语言（C++/Rust/Java/C#）**禁用** `EXTEND`，sema 直接报错并建议改为本地包装函数；
   - 仅 Python / Ruby / JavaScript 允许，且语义明确为"宿主语言层 monkey-patch"，不进入 `.ploy` 类型系统；
   - 文档中英双语注明该语义边界。

4. 文档：
   - 同步 spec / realization / USER_GUIDE / tutorial（中英双语）；
   - `tests/samples/` 新增 `34_default_args/`、`35_extend_dynamic/`，并在 `08_delete_extend/` README 中加入"limitations"说明。

5. 测试：
   - 默认值 sema / lowering / 跨语言传递；
   - `EXTEND` 在静态语言上的拒绝诊断。

6. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-12

[P1 可扩展性] `CONFIG` 包管理器字符串化。

1. 旧形式：`CONFIG VENV "<path>"` / `CONFIG CONDA "<env>"` / `CONFIG UV "<path>"` / `CONFIG PIPENV "<path>"` / `CONFIG POETRY "<path>"`。
2. 新形式：`CONFIG <language> "<package_manager>" "<path_or_env>";`
   - 例：`CONFIG python "venv" ".venv";` / `CONFIG python "conda" "myenv";` / `CONFIG rust "cargo" ".";` / `CONFIG javascript "npm" "./node_modules";` / `CONFIG java "maven" "./pom.xml";` / `CONFIG dotnet "nuget" "./packages";` / `CONFIG ruby "bundler" "./Gemfile";` / `CONFIG go "gomod" "./go.mod";`。
3. 旧关键字 `VENV` `CONDA` `UV` `PIPENV` `POETRY` 词法兼容期保留，sema 给 deprecation 警告并自动按新形式语义化。
4. 内部以**注册表**（`runtime` 或 `tools/polyc/src/config_registry.{h,cpp}`）持有 `<language, package_manager> -> handler` 映射，新增包管理器只需注册不需改词法。
5. 文档：
   - spec / realization / USER_GUIDE / tutorial 中英双语全部以新形式书写示例；
   - `docs/realization/package_management_*.md` 新增"如何注册自定义包管理器"小节。
6. 测试：
   - 单测覆盖新形式 + 旧形式 deprecation；
   - 集成：`tests/samples/04_package_import` 增加 `npm` / `cargo` / `maven` 镜像版本。
7. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-13

[P2 错误处理] 引入 `TRY` / `CATCH` / `THROW` 与跨语言异常桥接。

1. 语法：
   ```ploy
   TRY {
     LET x = CALL(python, foo, 1);
   } CATCH (e: Error) {
     // handle
   } FINALLY {
     // optional
   }
   ```
   - 表达式形式（短路传播）：`LET x = CALL(python, foo, 1)?;`，遇错向上抛出。
2. 类型：
   - 新增内建 `Error` 句柄类型，含 `message: String`、`source_lang: String`、`stacktrace: List<String>`；
   - 跨语言异常桥接：Python `Exception`、C++ `std::exception`、Java `Throwable`、C# `Exception`、Rust `Result::Err` 统一封装为 `Error`。
3. 运行时：
   - `runtime/` 新增 `error_bridge.{h,c}`，对外提供 `__ploy_rt_throw` / `__ploy_rt_catch_begin` / `__ploy_rt_catch_end`；
   - 各语言 bridge 拦截目标语言异常并转换为统一 `Error`；
   - 反向：`THROW` 在跨语言调用栈上抛出可被宿主侧 `catch` 的对应异常。
4. 文档：
   - `docs/realization/error_handling_*.md` 新建中英双语；
   - spec / USER_GUIDE / tutorial 中英双语新增"错误处理"章节。
5. 测试：
   - 单测：sema、lowering、运行时 bridge；
   - 集成：`tests/samples/36_try_catch/`，覆盖 5 种宿主语言异常 → `.ploy` 捕获、`.ploy` `THROW` → 宿主捕获。
6. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-14

[P2 异步] 引入 `ASYNC` / `AWAIT` 与跨语言异步桥接。

1. 语法：
   - `ASYNC FUNC fetch() -> i32 { … }`；
   - `LET v = AWAIT call_async();`；
   - `ASYNC` 函数返回值类型隐式包装为 `Future<T>`（IDE 显示原始 `T`）。
2. 调度模型：
   - 单线程协作式事件循环 + 可选多线程 work-stealing 池（接现有 `runtime/threading.{h,cpp}`）；
   - 跨语言：
     * Python `asyncio` coroutine ↔ `.ploy` `Future` 互转；
     * Rust `Future` 通过 `polyrt` 安装的 executor 驱动；
     * C++20 `std::coroutine` / `co_await` 适配；
     * Java `CompletableFuture` / .NET `Task<T>` 适配。
3. 运行时：
   - `runtime/` 新增 `async_bridge.{h,c}`、`event_loop.{h,c}`；
   - `polyrt async` 子命令：状态查看 / 任务列表 / 队列长度。
4. 文档：
   - `docs/realization/async_model_*.md`；
   - USER_GUIDE / tutorial 中英双语补"异步与并发"章节；
   - 已有 `14_async_pipeline` 升级为真实 `ASYNC/AWAIT` 实现。
5. 测试：
   - 单测：调度公平性、取消、异常传播；
   - 集成：`tests/samples/37_async_await/` 跨 5 种语言异步串/并联。
6. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-15

[P2 抽象] 引入泛型 `FUNC<T: Bound>` / `STRUCT<T>`。

1. 语法：
   - `FUNC max<T: Comparable>(a: T, b: T) -> T { IF a > b { RETURN a; } ELSE { RETURN b; } }`；
   - `STRUCT Pair<A, B> { first: A; second: B; }`；
   - 单态化（monomorphization）+ where 子句：`FUNC f<T>(x: T) WHERE T: Numeric { … }`。
2. 内建 trait / bound：
   - `Comparable` `Hashable` `Numeric` `Iterable` `Display`；
   - 在 spec 中明确每个 bound 对应宿主语言的契约（C++ `concept`、Rust `trait`、Java `interface`、C# `interface`、Python `Protocol`）。
3. 跨语言：
   - 泛型实例化在 `.ploy` 端完成，每个具化版本生成独立 stub；
   - sema 校验泛型参数与跨语言签名兼容。
4. 文档：
   - `docs/realization/generics_*.md` 新建中英双语；
   - spec / USER_GUIDE / tutorial 中英双语新增"泛型"章节。
5. 测试：
   - 单测：单态化、bound 检查、错误诊断；
   - 集成：`tests/samples/38_generics/` 演示泛型容器跨 C++/Python/Rust。
6. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-16

[P2 模块化] 可见性修饰 `PUB` / `PRIVATE` 与属性 / 注解 `@inline` 等。

1. 可见性：
   - 默认 `PRIVATE`（模块内可见）；显式 `PUB` 才跨模块可见；
   - `EXPORT` 只能用于 `PUB` 符号，否则 sema 报错；
   - 模块边界以文件为单位，未来可扩展 `MODULE name { … }` 嵌套。
2. 属性 / 注解：
   - 内建：
     * `@inline` / `@noinline` / `@always_inline`；
     * `@hot` / `@cold`；
     * `@profile` / `@no_profile`（与 2026-04-28-5 Profiler 联动）；
     * `@deprecated("msg")`；
     * `@link_name("symbol")`（覆盖 mangle 后的导出名）；
     * `@target("x86_64,arm64")`（限定可用架构）。
   - 解析层 `@<ident>(<args>)` 通用，未识别的注解仅 sema 警告。
3. 文档：
   - spec / realization / USER_GUIDE / tutorial 中英双语补两节；
   - `docs/specs/attribute_catalog_*.md` 新建中英双语，集中列出所有内建注解。
4. 测试：
   - 单测：可见性违反、各注解的 sema / 优化器交互；
   - 集成：`tests/samples/39_visibility_attrs/`。
5. 版本：minor 递进。完成后追加 `--end -done`。

--end -done


2026-04-28-17

[P2 字面量] 字符串字面量扩展：raw / 多行 / 模板。

1. 字面量形式：
   - 普通：`"hello\n"`；
   - Raw：`r"C:\path\no\escape"`、`r#"contains "quotes""#`；
   - 多行：`"""line1\nline2"""` 或缩进感知 `"""\n  line1\n  line2\n"""`；
   - 模板：`f"x = {x}, sum = {a + b}"`，编译期展开为 `format(...)` 调用，sema 检查内嵌表达式类型可格式化。
2. 跨语言：
   - 模板插值仅在 `.ploy` 侧展开；最终传给宿主语言的是已格式化字符串，与现有 marshalling 100% 兼容；
   - raw / 多行字符串便于嵌入 SQL / JSON / 跨语言代码片段（与 `samples/22_database_access`、`20_json_pipeline` 联动）。
3. 词法 / parser：
   - 新增 lexer 分支处理 `r"..."` / `"""..."""` / `f"..."`；
   - 模板字符串 AST 拆为 `string_part + expr_part` 序列。
4. 文档：
   - spec §3.3 / realization §3.3 / USER_GUIDE / tutorial 中英双语补"字符串字面量"章节；
   - `tests/samples/` 在已有 string / SQL / JSON 示例中切换为新字面量。
5. 测试：
   - lexer 单测覆盖所有分支与转义；
   - sema 模板插值类型检查；
   - 集成：模板字符串跨语言传输。
6. 版本：minor 递进（patch 也可，视字面量影响判断）。完成后追加 `--end -done`。

--end -done


2026-04-28-18

[P3 收尾] 小语法瑕疵集合。

1. `IF / WHILE / FOR` 外层括号设为可选：
   - `IF cond { … }` 与 `IF (cond) { … }` 等价；
   - parser 二者皆受。
2. `OPTION<T>` 解包：
   - 新语法 `IF LET Some(x) = opt { … } ELSE { … }`；
   - `?` 后缀短路（与 2026-04-28-13 错误传播复用 token，需明确二者上下文区分规则）。
3. 命名澄清：
   - `LIST<T>` 在 spec 中加显著说明"非链表，等价 Rust `Vec<T>` / C++ `std::vector<T>`"；
   - 在文档与 IDE tooltip 中提示。
4. `NULL` 限定：
   - sema 仅在与裸指针类型互操作时允许；与 `OPTION<T>` 互用时报错并建议改用 `None`；
   - 文档明确两者区别。
5. 文档注释：
   - 新增 `///` 文档注释，紧邻 `FUNC` / `STRUCT` / `LET` 等顶层声明时收集为 doc；
   - 新增工具 `polydoc`（`tools/polydoc/`），从 `.ploy` 抽取 doc 输出 Markdown / JSON；
   - 接入 IDE 悬浮提示。

6. 文档：
   - spec / realization / USER_GUIDE / tutorial 中英双语全部小修；
   - `docs/api/polydoc_*.md` 新建中英双语。
7. 测试：
   - parser：可选括号、`IF LET`、`?`；
   - polydoc：抽取与渲染。
8. 版本：minor 递进。完成后追加 `--end -done`。

--end -done

2026-04-28-19

[IDE P0-1] LSP 客户端框架 + Polyglot Language Server 雏形。

1. 在 `tools/ui/common/` 下新增 `lsp/`：
   - `lsp_client.{h,cpp}`：JSON-RPC over stdio / TCP；支持 `initialize` / `shutdown` / `textDocument/{didOpen,didChange,didSave,didClose}` / 通用请求转发；
   - `lsp_message.{h,cpp}`：LSP 标准消息类型（Position / Range / Diagnostic / Location / Hover / CompletionItem / SignatureHelp / SymbolInformation / CodeAction / TextEdit / WorkspaceEdit / DocumentSymbol）；
   - `lsp_session.{h,cpp}`：以 (workspace_uri, language_id) 为键管理多服务器会话；
   - `lsp_capability_registry.{h,cpp}`：服务器能力协商缓存；
   - `lsp_log_panel.{h,cpp}`：可在 IDE 内查看 LSP 通信日志（请求 / 响应 / 通知，可过滤）。

2. 新增 `tools/polyls/` —— PolyglotCompiler 自研 Language Server：
   - 入口 `polyls.cpp`、库 `polyls_core/`；
   - 复用 `frontend_ploy` + `frontend_cpp` + `frontend_python` + `frontend_rust` + `frontend_java` + `frontend_dotnet` 共享分析能力，提供 `.ploy` 与跨语言基础能力；
   - 本条只交付 `initialize` / `textDocument/didOpen` / `didChange` / `didClose` / `publishDiagnostics`（仅语法错误）；
   - 其它能力（completion / hover / definition）作为 capabilities advertised = false，留给后续条目实现。

3. 编辑器接入：
   - `code_editor` 增加变更事件桥接，按 debounce 200ms 转发为 LSP `didChange`；
   - 新增 gutter `LspDiagnosticsModel`，把 `publishDiagnostics` 渲染为标尺图标 + 行下划线。

4. 配置：
   - `Settings → Language Servers` 页：每语言可配置 server 命令、参数、env、初始化选项；
   - 默认值：`.ploy` 走自研 `polyls`，C++ 走 `clangd`，Python 走 `pyright`，Rust 走 `rust-analyzer`，Java 走 `jdtls`，C# 走 `omnisharp`；
   - 对应可执行不在 PATH 时给予非阻塞通知 + 修复指引。

5. 测试：
   - `tests/unit/polyui/lsp_client_test.cpp`：mock server 收发；
   - `tests/unit/polyls/lifecycle_test.cpp`：initialize / shutdown / cancel；
   - `tests/integration/`：打开 `.ploy` 故意语法错，校验诊断出现在 LSP 通道与 IDE gutter。

6. 文档：
   - `docs/specs/lsp_integration_*.md`（中英双语）；
   - `docs/api/polyls_*.md`（中英双语）；
   - USER_GUIDE / tutorial 中英双语新增"语言服务器架构"章节。

7. 版本：根 `CMakeLists.txt` minor 递进。规则 2/3/10 全部生效；不允许任何占位 / TODO；完成后追加 `--end -done`。

--end -done


2026-04-28-20

[IDE P0-2] 实时诊断 / Problems Panel / 编辑即检查。

1. 新增面板 `tools/ui/common/{src,include}/problems_panel.{h,cpp}`：
   - 按 (severity, source, file) 聚合；
   - 支持过滤：error / warning / info / hint，按文件 / 按 LSP 来源 / 按正则；
   - 双击跳转源码并高亮范围；
   - 状态栏新增"E:N W:N H:N"统计与一键打开 Problems Panel 的入口。

2. 实时检查：
   - 编辑停顿 ≥ 200ms 触发 `didChange`；
   - 全工作区背景检查：保存或文件创建/删除/重命名时，对受影响文件做增量；
   - 大型工作区（>2000 文件）首检异步分批，进度显示在状态栏。

3. 与编译器联动：
   - `polyc --check <file>` 在无 LSP 可用时作为 fallback；
   - 编译产生的诊断（spec / sema / lowering）转换为 LSP `Diagnostic` 上送 Problems Panel。

4. 测试：
   - `problems_panel_model_test.cpp`：聚合 / 过滤 / 排序；
   - 集成：构造含错样例，校验 Problems 面板内容与 gutter 一致。

5. 文档：
   - `docs/realization/problems_panel_*.md`（中英双语）；
   - USER_GUIDE / tutorial 同步。

6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-21

[IDE P0-3] 代码补全（Completion）+ 签名帮助（Signature Help）+ 悬浮（Hover）。

1. 编辑器：
   - 触发器：`.` `:` `::` `(` 字符 + 手动 `Ctrl+Space`；
   - UI 弹窗：图标 / 类型 / 文档摘要 / 详情面板（左半选择，右半 detail）；
   - Snippet 占位符 + Tab 跳转；
   - 模糊匹配 + 评分（前缀 > 子序列 > 模糊），可在设置中切换策略。

2. Hover：
   - 鼠标悬停 / `Ctrl+K Ctrl+I` 调出；
   - 渲染 Markdown，含签名 + 文档 + 跨语言来源（例如悬停 `.ploy` 中 `cpp::math::add` 时，显示 C++ 端真实签名 + 文件链接）。

3. Signature Help：
   - 输入 `(` 自动出现；
   - 高亮当前参数；
   - 显示重载列表（来自 LSP `signatureHelp`），上下键切换。

4. polyls 能力：
   - 实现 `textDocument/completion`、`completionItem/resolve`、`textDocument/hover`、`textDocument/signatureHelp`；
   - `.ploy` 端给出关键字 / 已声明符号 / 已 `IMPORT` 模块的成员补全；
   - 跨语言：当上下文是 `LINK <lang>::` 时，列出该语言已索引模块。

5. 测试：
   - polyls 单测：`completion_test.cpp` `hover_test.cpp` `signature_help_test.cpp`；
   - UI 单测：补全弹窗模型；
   - 集成：在 `.ploy` 输入 `LINK cpp::` 弹出补全 ≥ N 项。

6. 文档：USER_GUIDE / tutorial 中英双语。
7. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-22

[IDE P0-4] 跳转：Definition / Declaration / Implementation / References / Type Definition。

1. polyls：
   - `textDocument/definition` / `declaration` / `implementation` / `typeDefinition` / `references`；
   - 索引：在 sema 阶段产出 `SymbolIndex`（持久化到 `.polyc-cache/`），覆盖 `.ploy` 与已 `IMPORT` 模块；
   - 增量更新：`didChange` 增量重建。

2. 编辑器：
   - 快捷键：`F12` 跳转定义；`Ctrl+F12` 实现；`Shift+F12` 引用；`Ctrl+K F12` 在侧栏 Peek；
   - Peek 视图：行内嵌入式预览（不离开当前文件）。

3. 跨语言：
   - 在 `.ploy` 跳转 `LINK` 目标符号 → 直接打开宿主语言文件并定位；
   - 反向：宿主语言中"被哪些 `.ploy` LINK 引用"通过 `references` 返回。

4. 测试：
   - 单测：索引正确性、增量正确性、跨语言映射；
   - 集成：`tests/samples/09_mixed_pipeline` 上端到端跳转矩阵。

5. 文档：`docs/realization/symbol_index_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-23

[IDE P0-5] 重构（Refactor）：rename / extract function / inline / move file / change signature。

1. polyls 能力：
   - `textDocument/prepareRename` / `rename`；
   - `textDocument/codeAction`：extract function、inline variable、inline function、change signature、move file；
   - 返回 `WorkspaceEdit` 跨文件原子应用。

2. 编辑器：
   - Rename 双重确认：弹窗显示影响列表（按文件分组），可逐项勾选；
   - Extract function：选中区域 → `Ctrl+Shift+R` → 输入新函数名 + 自动推断参数 / 返回；
   - 撤销栈：所有重构进单步 undo。

3. 跨语言重命名：
   - `.ploy` 中重命名 `LINK` 名 → 同步更新 LINK / EXPORT / 引用；
   - 宿主语言端重命名（若 LSP 支持）→ polyls 监听变更，自动更新 `.ploy`。

4. 测试：
   - 单测：rename 范围正确性、不该改的不被改；
   - 集成：跨语言 rename 端到端。

5. 文档：`docs/realization/refactoring_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-24

[IDE P0-6] 语义级语法高亮 + Tree-sitter 集成。

1. 接入 tree-sitter：
   - `tools/ui/common/syntax/tree_sitter_runtime.{h,cpp}`：runtime 加载 / 解析 / 增量重解析；
   - 引入 grammar：`tree-sitter-ploy`（自研，新增到 `tools/polyls/grammar/`）+ 第三方 grammar 仓子模块（cpp / python / rust / java / c-sharp）；
   - 解析结果用于：① 折叠区间；② 大纲；③ 选区扩展（Smart Select）；④ 语义高亮。

2. LSP `textDocument/semanticTokens`：
   - polyls 实现完整 token 类型 + modifier；
   - 高亮主题与 `theme_manager` 颜色变量统一。

3. 编辑器：
   - 旧 `syntax_highlighter`（正则版）保留作为 LSP 不可用时的 fallback；
   - 设置项："Use LSP semantic tokens"（默认 on）。

4. 测试：
   - tree-sitter parser 加载 / 增量；
   - semanticTokens 单测；
   - 视觉对比快照（仅 token 类型序列）。

5. 文档：`docs/realization/semantic_highlight_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-25

[IDE P1-1] 多标签 / 分屏 / 拖拽分组 / Quick Open / 全局搜索 / Outline / Breadcrumbs / Minimap。

1. 多标签 + 分屏：
   - `tools/ui/common/editor/editor_group.{h,cpp}`、`editor_grid.{h,cpp}`；
   - 支持横/纵分屏、最多 4×4 网格；
   - 标签拖拽到分组 / 拆分 / 合并；
   - 标签固定 / pin / 关闭其他 / 关闭右侧。

2. Quick Open：
   - `Ctrl+P`：模糊文件名（VS Code 风格 ranking），含最近文件优先；
   - `Ctrl+Shift+P` 已有命令面板（保留）。

3. Symbol Search：
   - `Ctrl+T`：跨工程符号（`workspace/symbol`）；
   - `Ctrl+Shift+O`：当前文件符号（`documentSymbol`）。

4. 全局搜索 / 替换：
   - `Ctrl+Shift+F`：正则 / 大小写 / 全词 / glob include / glob exclude；
   - 替换支持捕获组；
   - 流式渲染结果（不阻塞 UI）。

5. Outline / Breadcrumbs / Minimap：
   - Outline 面板：树视图 + 折叠 + 过滤；
   - Breadcrumbs：文件路径 + 当前符号；
   - Minimap：右侧迷你图（可关闭）。

6. 测试：
   - editor group 模型；
   - quick open ranker；
   - 全局搜索结果模型；
   - outline 模型。

7. 文档：USER_GUIDE / tutorial 中英双语集中一节。
8. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-26

[IDE P1-2] 多光标 / 列编辑 / 折叠 / 自动格式化 / Snippets / EditorConfig。

1. 多光标：
   - `Alt+Click` 加光标；`Ctrl+Alt+↑/↓` 上下扩展；`Ctrl+D` 选下一个相同；`Ctrl+Shift+L` 全选相同；
   - 矩形选区：`Shift+Alt+拖拽`。

2. 折叠：
   - 基于 tree-sitter 的语义折叠（函数 / 块 / 注释 / 区域 `// region` `// endregion`）；
   - `Ctrl+K Ctrl+0/J` 折叠所有 / 展开所有。

3. Formatter：
   - LSP `textDocument/formatting` / `rangeFormatting` / `onTypeFormatting`；
   - polyls 集成内建 `.ploy` 格式化器；外部语言走对应 LSP；
   - 设置："Format on save" / "Format on paste"。

4. Snippets：
   - 用户片段（JSON）+ 内建片段库；
   - 占位符 / 选项 / 变量（`$CURRENT_DATE` 等）；
   - 自动出现在补全列表。

5. EditorConfig：
   - 解析 `.editorconfig`：缩进 / 行尾 / 编码 / 末行换行；
   - 状态栏显示当前生效配置。

6. 测试：
   - 多光标编辑；折叠区间；Format on save；Snippet 展开；EditorConfig 解析。

7. 文档：USER_GUIDE / tutorial 中英双语。
8. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-27

[IDE P1-3] Git 进阶：diff 视图 / blame / merge resolver / SCM 抽象。

1. Diff 视图：
   - 行内 / 并排两种模式；
   - Hunk 接受 / 拒绝（stage / unstage）；
   - 与 LSP / 编辑器共存（diff 区域内仍可悬浮 / 跳转）。

2. Blame：
   - gutter 显示最近一次提交者 / 时间 / 提交标题；
   - 行级悬浮显示完整 commit 信息；可一键打开 commit 详情面板。

3. Merge Conflict Resolver：
   - 三向视图（current / incoming / result）；
   - 一键 Accept current / incoming / both；手动编辑结果区。

4. SCM 抽象：
   - `tools/ui/common/scm/scm_provider.{h,cpp}`：抽象 status / diff / commit / branch / log / blame；
   - 默认实现 `GitProvider`（替换/继承现有 `git_panel`）；
   - 预留 `MercurialProvider` / `SubversionProvider` 接口。

5. 测试：diff 算法对齐、blame 解析、merge resolver 操作完备。
6. 文档：USER_GUIDE / tutorial 中英双语。
7. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-28

[IDE P2-1] DAP（Debug Adapter Protocol）适配 + 通用调试 UI。

1. `tools/ui/common/dap/`：
   - `dap_client.{h,cpp}`：JSON-RPC over stdio；
   - 完整覆盖 `initialize` `launch` `attach` `setBreakpoints` `setExceptionBreakpoints` `setDataBreakpoints` `setFunctionBreakpoints` `configurationDone` `threads` `stackTrace` `scopes` `variables` `evaluate` `continue` `next` `stepIn` `stepOut` `pause` `disconnect` `terminate`；
   - 事件：`stopped` `continued` `exited` `terminated` `output` `breakpoint` `thread`。

2. 调试视图：
   - Call Stack / Threads / Variables / Watch / Scope / Debug Console；
   - 行内变量值（inline values）；
   - 日志点 / 条件断点 / 命中计数 / 异常断点。

3. 启动配置：
   - `polyui.launch.json` 等价物（位于 `.polyc/launch.json`），多配置切换；
   - 默认提供：`.ploy` Run/Debug、Python（debugpy）、C++（lldb/gdb/codelldb）、Rust（codelldb/lldb）、Java（jdtls/jdwp）、.NET（vsdbg/netcoredbg）。

4. 旧 `debug_panel` 重构：
   - 抽出共享接口供 DAP 与本地 polyrt 调试共用；
   - 不破坏现有最小调试能力。

5. 测试：
   - DAP mock 服务器；
   - 全套调试动作端到端（针对 sample 09/15）。

6. 文档：`docs/realization/dap_integration_*.md`（中英双语）+ USER_GUIDE 中英双语。
7. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-29

[IDE P2-2] 任务系统 + Run/Debug 配置 + Hot Reload。

1. 任务系统：
   - `.polyc/tasks.json`：build / test / lint / format / custom shell / compound 依赖；
   - 任务输出归类到 `output_panel` 子频道；
   - 终止 / 重跑 / "watch 模式（受 problemMatcher 解析）"。

2. Run/Debug 配置：
   - 与上一条 launch.json 协同；
   - 状态栏快捷选择 + 一键运行 / 调试。

3. Hot Reload / Edit-and-continue：
   - 对 `.ploy` 与 Python：增量重编 + 替换运行进程的相应模块；
   - 对 C++ / Rust：与 polyrt 配合，在调试态修改 → 编译 → 替换函数符号（仅在调试支持时）；
   - 对 Java / .NET：JDI / EnC 适配。

4. 测试：
   - 任务依赖图调度；
   - hot reload 单语言 + 跨语言；
   - launch 配置切换。

5. 文档：USER_GUIDE / tutorial 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-30

[IDE P3-1] Test Explorer + Coverage 视图 + Inline Run-Test。

1. Test Explorer：
   - 树形（项目 → 套件 → 用例）；状态彩色（pass / fail / skip / pending）；
   - 单点重跑 / 套件重跑 / 失败优先；
   - 适配 CTest（PolyglotCompiler 现有）+ pytest + cargo test + JUnit + xUnit/NUnit；
   - LSP `testing` 协议（VS Code Testing API 等价）抽象层。

2. 行内运行：
   - 在测试函数上方显示 ▶ Run / 🐞 Debug 行内按钮（CodeLens 风格）；
   - 单测失败时行号上挂诊断 + 一键打开 Test Explorer 详情。

3. Coverage：
   - gutter 红绿条 + 百分比；
   - 加载 lcov / cobertura / coverage.py / cargo-tarpaulin / dotnet coverlet 报告；
   - 工作区视图：按文件 / 目录树 + 阈值报警。

4. 测试：
   - 解析 5 种报告格式；
   - 行内 CodeLens 渲染。

5. 文档：USER_GUIDE / tutorial 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-31

[IDE P3-2] 包管理 UI + 依赖图 + 漏洞扫描 + REPL/Notebook。

1. 包管理 UI（与 demand 2026-04-28-12 `CONFIG` 字符串化呼应）：
   - 一处管理 venv / conda / uv / pipenv / poetry / cargo / npm / maven / gradle / nuget / gem / gomod；
   - 安装 / 升级 / 移除 / 锁文件查看 / 与 `.ploy CONFIG` 双向同步；
   - 状态栏显示当前激活环境。

2. 依赖图：
   - 树 + 图两种视图；版本冲突高亮；可导出 SVG。

3. 漏洞扫描：
   - 接入开源 advisory 数据源（osv.dev / GitHub Advisory）；
   - 行内 + 面板告警；可关闭。

4. REPL / Notebook：
   - 内置 `.ploy` REPL（基于 `polyc --repl`，需 `polyc` 同步实现）；
   - Python REPL / IRust / IRB / dotnet-script 嵌入；
   - 简易 Notebook 视图（cell + 输出 + 跨语言 `LINK` 单元）。

5. 测试：
   - 各包管理器抽象层端到端；
   - REPL / Notebook 子集。

6. 文档：USER_GUIDE / tutorial 中英双语。
7. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-32

[IDE P4-1] 跨语言独有：跳转 / Hover / 重命名 / Bridge 视图 / Marshalling 可视化。

1. 跨语言跳转 / Hover / 重命名：
   - 在 `2026-04-28-22` / `-23` 基础上做"跨语言加强"：
     * `.ploy` 中 `LINK cpp::math::add` 直接 F12 跳到 C++ 源；
     * 反向：宿主语言函数定义处显示"X .ploy LINK references"CodeLens；
     * 跨语言 rename 通过 polyls 协调多个 LSP 一致提交 `WorkspaceEdit`。

2. Bridge 视图：
   - 新面板 `tools/ui/common/bridge_panel.{h,cpp}`：
     * 列出当前工作区所有跨语言桥；
     * 显示生成的 stub 名 / marshalling 策略 / 调用次数（运行期来自 polyrt calltrace）；
     * 双击跳源。

3. Marshalling 可视化：
   - 选中 `LINK` / `CALL` / `METHOD` 时，侧栏渲染参数 / 返回值的转换链路（IR 层 → marshalling helper → 目标 ABI）；
   - 链路上每一步可点开看代码片段（来自 lowering 输出）。

4. 测试：
   - bridge 列表与编译产物一致；
   - marshalling 链路渲染对 5 种宿主语言均可用。

5. 文档：`docs/realization/cross_language_ide_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-33

[IDE P4-2] Compile Pipeline Inspector + IR Viewer / Diff + Asm Viewer + Source↔Asm 联动。

1. Pipeline Inspector：
   - 新面板 `pipeline_inspector_panel`：分阶段（frontend / sema / IR-pre-opt / IR-post-opt / backend asm / link）展示 `aux/` 产物；
   - 阶段间耗时直方图；点开任一阶段查看相关产物。

2. IR Viewer / Diff：
   - 文本 + 折叠（按函数 / 基本块）；
   - 同函数 pre/post 优化对比（diff 视图，不可编辑）；
   - 跳转：源行 ↔ IR 行 ↔ 资产行三向联动。

3. Asm Viewer：
   - Compiler Explorer 风格：左源右汇编；hover 行高亮对端；
   - 支持 x86_64 / arm64 / wasm；
   - 与 `polyasm` / backend disassembler 联动。

4. 测试：
   - 联动定位精度（行 ↔ IR ↔ asm 误差 ≤ 1）；
   - 大文件分块加载性能（≥ 10MB IR 渲染流畅）。

5. 文档：USER_GUIDE / tutorial 中英双语 + `docs/realization/pipeline_inspector_*.md`。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-34

[IDE P4-3] Sample / Tutorial Browser + Topology Live + 类型推断浮层。

1. Sample / Tutorial Browser：
   - 新面板：内置 `tests/samples/` 与 `docs/tutorial/` 索引；
   - 一键"打开为工作区副本"（避免污染源）；
   - 标签筛选（语言 / 主题 / 难度）；
   - 与 `2026-04-28-4` 新增的 17–30 样例联动。

2. Topology Live：
   - `topology_panel` 已有静态视图；本条加：
     * 实时跟随当前编辑文件 / 当前选中函数；
     * 鼠标悬停拓扑节点 → 编辑器跳转；
     * 编辑后 debounce 重算并增量更新拓扑。

3. 类型推断浮层（Inlay Hints）：
   - LSP `textDocument/inlayHint`；
   - polyls：`LET m = NEW(python, ...)` 行尾以灰字显示推断 `: HANDLE<python::torch::nn::Linear>`；
   - 参数名 hint：`f(/*x:*/ 1, /*y:*/ 2)`；
   - 设置可关。

4. 测试：
   - sample browser 模型；
   - topology 实时增量正确；
   - inlay hint 正确性。

5. 文档：USER_GUIDE / tutorial 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-35

[IDE P5-1] Remote Development（SSH / WSL / Container / Dev Container）。

1. 架构：
   - 引入 `tools/ui/common/remote/`：
     * `remote_session.{h,cpp}`：抽象远端文件系统 / 进程 / 端口转发；
     * 实现 `SshRemote` / `WslRemote` / `ContainerRemote`（docker / podman）；
   - polyls / DAP / 任务系统 / 终端 全部走 remote_session 抽象，本地视为 `LocalRemote`。

2. Dev Container：
   - 解析 `.devcontainer/devcontainer.json`；
   - 一键"在容器中重新打开"；
   - 容器内自动安装 polyls / 必要 LSP / 包管理工具。

3. 端口转发 / 文件同步 / 远端终端 完整可用。

4. 测试：
   - 单测：抽象层；
   - 集成（CI 中 SSH 与容器场景）：在远端跑 sample 09 全链路。

5. 文档：`docs/realization/remote_dev_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-36

[IDE P5-2] AI 助手集成（行内补全 / 聊天 / 解释 / 重构建议）+ 协作 / PR 集成。

1. AI 助手框架：
   - `tools/ui/common/ai/`：
     * `ai_provider.{h,cpp}`：抽象 chat / completion / inline-suggest / refactor-suggest；
     * 适配器：本地 ollama、OpenAI 兼容 API、Azure、Anthropic（仅 SDK 转发，不内置 key）；
   - 隐私：默认本地优先；任何外送都需用户显式同意；
   - 提示模板与"项目上下文采集"可在设置中配置（哪些目录可上送 / 哪些禁止）。

2. UI：
   - 行内灰字建议（Tab 接受 / Esc 拒绝 / Alt+] 切换备选）；
   - Chat 面板（带文件 / 选区 / Diagnostics 上下文一键插入）；
   - Refactor Diff 面板（建议变更以 diff 形式呈现，逐 hunk 接受）。

3. 协作 / PR：
   - GitHub / GitLab / Gitea 三套 provider；
   - PR 列表 / 评论 / Diff Review / Push to PR；
   - Issue 视图：列表 / 创建 / 关联 commit / 引用文件行。

4. 测试：
   - mock provider 单测；
   - 协作 provider 适配层单测；
   - 集成：本地 ollama 端到端聊天 + 行内建议接受。

5. 文档：`docs/realization/ai_integration_*.md`、`docs/realization/collab_*.md`（中英双语）+ USER_GUIDE 中英双语 + 隐私声明。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-37

[IDE P6-1] 插件系统（Extension API + 本地 Marketplace 雏形）+ Workspace / Multi-root。

1. Extension API：
   - `tools/ui/common/ext/`：
     * 加载形态：动态库（C/C++）+ JavaScript/TypeScript（嵌入式 QuickJS / V8 视构建条件可选）；
     * Manifest：`extension.json`（id / name / version / activation / contributes）；
     * 贡献点：commands / keybindings / menus / panels / views / status-bar items / themes / language-clients / debug-adapters / file-icon-themes / formatters / snippets / tasks / refactor-providers；
   - 安全：沙箱 + 能力授权（fs / network / process）显式声明 + 用户审批。

2. 本地 Marketplace：
   - 文件系统 / HTTP 索引；
   - 安装 / 卸载 / 更新 / 版本回滚；
   - 签名校验（可选）。

3. Workspace / Multi-root：
   - `polyui.code-workspace` 等价：多根 + 每根独立设置 + 跨根搜索 / 跳转；
   - 与 LSP / DAP / 任务系统协同。

4. 测试：
   - 加载 / 卸载 / 重载；
   - 贡献点注册去重；
   - 多根工作区下的 LSP 实例隔离。

5. 文档：`docs/api/extension_api_*.md`（中英双语，是大型文档）+ `docs/realization/marketplace_*.md`（中英双语）+ USER_GUIDE 中英双语。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-38

[IDE P6-2] Welcome / Notifications / Status Bar 可定制 / Recent / 会话恢复 / Bookmarks / TODO 索引。

1. Welcome Page：
   - 启动展示：最近工作区、教程入口、样例入口、新特性提示；
   - 可关闭 / 可固定。

2. Notification Center：
   - 持久化 + 分级 + action 按钮 + 不打扰模式；
   - 状态栏图标显示未读数。

3. Status Bar 可定制：
   - 用户可拖拽显示 / 隐藏：分支 / 编码 / 行尾 / 缩进 / 当前语言 / 语言服务器状态 / 包管理器 / Profiler 状态 / 错误警告统计；
   - 第三方插件可注册。

4. Recent Files / Workspaces：
   - `Ctrl+R` 切换最近工作区；
   - `Ctrl+E` 最近文件。

5. 会话恢复：
   - 重启后还原：标签 / 滚动 / 光标 / 折叠 / 分屏布局 / 面板大小 / 调试视图状态；
   - 可关闭。

6. Bookmarks：
   - `Ctrl+Alt+K` 加 / 去；
   - 书签面板：跨文件列表 + 标签 + 颜色。

7. TODO / FIXME 索引：
   - 后台扫描 + 面板汇总 + 自定义关键字（如 `XXX` `HACK`）。

8. 测试：
   - 各面板模型；
   - 会话序列化 / 反序列化；
   - 通知不打扰策略。

9. 文档：USER_GUIDE / tutorial 中英双语集中一节。
10. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-39

[IDE P6-3] i18n 多语言 UI + 无障碍（Accessibility）+ 遥测 / 反馈 / 崩溃报告（可关闭）。

1. i18n：
   - `tools/ui/common/i18n/`：基于 Qt `QTranslator` + 自有字符串目录；
   - 内建语言：中文（简/繁）/ 英文 / 日文 / 韩文；
   - 字符串 ID 化，禁止源码硬编码 UI 字串；
   - 文档：贡献指南 `docs/realization/i18n_*.md`（中英双语）。

2. 无障碍：
   - 所有可聚焦控件键盘可达；
   - 屏幕阅读器（NVDA / JAWS / VoiceOver / Orca）兼容；
   - 高对比度主题 / 大字体模式 / 减少动效；
   - 文档：`docs/realization/accessibility_*.md`（中英双语）。

3. 遥测 / 反馈 / 崩溃：
   - 默认**关闭**；启用需用户同意 + 可随时撤回；
   - 收集字段最小化 + 本地预览（用户可见）+ 端到端透明；
   - 崩溃报告本地落盘 → 可选上传；
   - 隐私文档 `docs/realization/telemetry_*.md`（中英双语）。

4. 测试：
   - 切换语言、屏幕阅读器 smoke、崩溃模拟、遥测开关；
   - i18n 缺失字串扫描器作为 CI 检查。

5. 文档：USER_GUIDE / tutorial 中英双语；隐私声明合规中英双语单独成档。
6. 版本：minor。完成后追加 `--end -done`。

--end -done


2026-04-28-40

[IDE P6-4] 文件类型查看器：Image / Hex / Binary / 数据库客户端 / SQL Console。

1. Image Viewer：
   - PNG / JPEG / WebP / GIF / SVG / BMP；缩放 / 平移 / 通道分离 / 像素拾取。

2. Hex Viewer：
   - 大文件分块（≥ 1GB）；定位 / 检索 / 跳转 / 高亮区段；
   - 与 `aux/` 二进制产物（`.ir.bin` / `.asm.bin` / `.obj`）联动，按已知 schema 渲染字段。

3. Binary 类型识别 / 反汇编：
   - 识别 ELF / PE / Mach-O / WASM；
   - 简化反汇编（与 `polyasm` 共享）。

4. 数据库客户端 / SQL Console：
   - 支持 SQLite（首批，与 `samples/22_database_access` 联动）；
   - 表浏览 / 行编辑 / SQL 执行 / 结果分页 / 导出 CSV。

5. 测试：
   - 图像渲染、Hex 大文件性能、二进制识别、SQL 端到端。

6. 文档：USER_GUIDE / tutorial 中英双语。
7. 版本：minor。完成后追加 `--end -done`。

--end -done



2026-04-28-41

[BIN-1] 二进制容器统一抽象与 target triple 解析（Container / TargetTriple）。

背景：当前 polyld 的 `OutputFormat::kExecutable` 在 `GenerateOutput()` 里硬编码调用 `GenerateELFExecutable()`，从不查 `target_os`；`pe_writer.cpp` 已实现 PE32+ 但完全未接入；`GenerateMachOExecutable()` 仅被 `GenerateMachODylib()` 内部调用一次，对外不可达。结果 Windows 宿主跑出来的 `.exe` 实际是 ELF（≈ 423 字节，魔数 7F 45 4C 46），双击报"不是有效 Win32 应用"。本条建立统一抽象，后续条目在其上派发。

1. `common/include/target_triple.h` + `common/src/target_triple.cpp`：
   - 新增 `struct TargetTriple { Arch arch; Vendor vendor; OS os; Env env; SubArch sub; };`
   - 解析 `x86_64-pc-windows-msvc` / `aarch64-apple-darwin` / `x86_64-unknown-linux-gnu` / `wasm32-wasi` / `aarch64-pc-windows-msvc` 等全部主流 triple；非法输入 `Result<TargetTriple, ParseError>` 返回（不抛、不退出）。
   - 提供 `TargetTriple HostTriple()`：从 `_WIN32 / __APPLE__ / __linux__ / __aarch64__ / _M_X64` 推导。
   - 同时提供 `std::string TargetTriple::str() const`、`bool operator==`、`hash`。

2. `common/include/binary_container.h`：
   - `enum class BinaryContainer { kAuto, kELF, kPE, kMachO, kWasm };`
   - `BinaryContainer ContainerForOS(OS)` 与逆向 `OS DefaultOSForContainer(BinaryContainer)`。
   - `BinaryContainer ResolveContainer(const TargetTriple& triple, BinaryContainer requested)`：`requested != kAuto` 优先；否则按 triple.os 推导；wasm32-wasi → kWasm。

3. polyc / polyld / 后端 都改用上述抽象：
   - polyld 的 `LinkerConfig` 增加 `BinaryContainer container{kAuto}; TargetTriple target_triple;`，与 `target_os` 字段双向兼容（旧字段保留，setter 解析 triple 后回填）。
   - polyc 命令行新增 `--container=auto|elf|pe|macho|wasm`（默认 auto），并把 `--target=<triple>` 透传给 polyld；现有 `--target-os=windows` 等仍旧可用，内部映射成 triple。
   - 后端 `EmitObjectCode()` 接受 `TargetTriple` 替代裸 `TargetArch`（保留旧重载，标记为转发到新 API，禁止出现 `[[deprecated]]` 之外的占位）。

4. 测试：`tests/unit/common/test_target_triple.cpp` 全表测试 50+ triple；`tests/unit/common/test_binary_container.cpp` 派发表测试。CI 在三平台上各跑一遍 host 推导。

5. 文档：`docs/realization/binary_containers_zh.md` + `_en.md`，包含 triple 解析表、派发规则、迁移指南、已知限制。同步更新 `README` 与 `tools/polyc/README` / `tools/polyld/README` 的中英双语段落。
6. 版本：根 `CMakeLists.txt` + `VERSION.txt`：`1.4.1` → `1.5.0-pre.1`（开启 1.5.0 周期；进入 release 时改为 `1.5.0`）。完成后追加 `--end -done`。

--end -done


2026-04-28-42

[BIN-2] polyld 输出派发：`Linker::GenerateOutput()` 真正按 Container 分发。

依赖：41。

1. `tools/polyld/include/linker.h`：
   - 保留 `OutputFormat`（语义"是 exe / 是 so / 是 .o / 是 .a"）不变；
   - 新增成员声明 `bool GeneratePEExecutable(); bool GeneratePEDll(); bool GenerateMachOExecutable(); bool GenerateMachODylib(); bool GenerateWasmModule();`（其中 Mach-O 两个已有声明，需迁移并补齐实现）；
   - `Linker` 内部维护 `BinaryContainer effective_container_`，由 `LinkerConfig.container` + `target_triple` 在 `Initialize()` 阶段解析一次。

2. `tools/polyld/src/linker.cpp` 改派发：
   - `GenerateOutput()` 查表：(`output_format`, `effective_container_`) → 具体生成函数；
   - 不可达分支返回结构化错误（沿用现有 `ReportError` 机制），不静默回退到 ELF；
   - 删除 `GenerateELFSharedLibrary()` 内部直接转 `GenerateELFExecutable()` 的伪实现，写真正的 `ET_DYN` 生成（保留旧函数符号但内部调真实现，不破坏 ABI）。

3. `polyc` 在透传到 polyld 时，按 container 决定文件后缀策略：
   - PE：`.exe / .dll / .lib`；
   - ELF：`(无后缀) / .so / .a`；
   - Mach-O：`(无后缀) / .dylib / .a`；
   - Wasm：`.wasm`。
   - 用户显式指定的 `-o` 路径不被覆写，但若后缀与 container 不匹配，发 `polyc-warn-W2101`。

4. 测试：`tests/unit/polyld/test_output_dispatch.cpp` 用 fake writer 注入，验证 (container, output_format) → 调用了对的写出函数；多 triple 矩阵（含 wasm32-wasi）。

5. 文档：在 41 的 `binary_containers_*.md` 追加"派发表"章节（中英双语）；更新 polyld README。
6. 版本：`1.5.0-pre.1` → `1.5.0-pre.2`。完成后追加 `--end -done`。

--end -done


2026-04-28-43

[BIN-3] PE32+ 写出器加固：多节、真实重定位、子系统、arm64-windows。

依赖：41 / 42。

1. `tools/polyld/src/pe_writer.cpp` 在现有 `BuildPE32PlusImage` 基础上扩展（不重写、不删旧 API）：
   - 多节支持：`.text / .data / .rdata / .bss / .pdata / .reloc / .xdata`；按现有 `output_sections_` 排布；正确填 `Characteristics`（`MEM_EXECUTE / MEM_READ / MEM_WRITE / CNT_CODE / CNT_INITIALIZED_DATA / CNT_UNINITIALIZED_DATA`）。
   - 真实 base relocation：把 `.text / .data` 内的绝对地址重定位收集成 `.reloc` 节，按页分块（4KB）写 `IMAGE_BASE_RELOCATION + WORD[]`；类型 `IMAGE_REL_BASED_DIR64`（x64）/ `IMAGE_REL_BASED_HIGHLOW`（x86）；arm64 `IMAGE_REL_BASED_ARM64_BRANCH26 / IMAGE_REL_BASED_ARM64_PAGEBASE_REL21 / IMAGE_REL_BASED_ARM64_PAGEOFFSET_12L / 12A`。
   - 子系统可配置：`BuildRequest.subsystem ∈ {Console, GUI, EFI_App, Driver}`；`polyc --subsystem=<...>` 透传；默认 Console。
   - `Machine` 字段按 triple.arch 选择：`IMAGE_FILE_MACHINE_AMD64 (0x8664)` / `IMAGE_FILE_MACHINE_ARM64 (0xAA64)` / `IMAGE_FILE_MACHINE_I386 (0x014C)`。
   - `DllCharacteristics`：默认开 `DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE | HIGH_ENTROPY_VA`（x64 / arm64）。
   - 异常处理表：x64 的 `.pdata`（`RUNTIME_FUNCTION[]`）+ `.xdata`（`UNWIND_INFO`）骨架，配合后端 emit 的 unwind 元信息（如后端尚未 emit，则按"无 unwind"模式正确填零长度，但仍生成空 `.pdata` directory entry，绝不省略）。
   - `.idata` / IAT：保留并增强为多 DLL 多函数；`HintNameTable` 正确写入；`BoundImport` 留为可选。

2. arm64-windows 端到端：smoke 测试在 host 是 x64 时也能通过 PE 头解析 + 反汇编工具识别（`dumpbin /headers /disasm:nobytes` 期望 `machine (ARM64)`）。

3. 测试：`tests/unit/polyld/test_pe_writer.cpp` 覆盖：
   - 多节布局正确（虚拟地址 / 文件偏移 / 对齐）；
   - `.reloc` 解码后回灌等于原始 RVA 集合；
   - 子系统 / Machine / DllCharacteristics 字段精确匹配；
   - 异常表/导入表交叉引用一致；
   - `dumpbin` 子进程断言（仅 Windows runner）。

4. 文档：`docs/realization/pe_writer_zh.md` + `_en.md`（新文档），含字段地图与调试 cheat sheet。
5. 版本：`1.5.0-pre.2` → `1.5.0-pre.3`。完成后追加 `--end -done`。

--end -done


2026-04-28-44

[BIN-4] `Linker::GeneratePEExecutable()` / `GeneratePEDll()` 实装并接入 pe_writer。

依赖：41 / 42 / 43。

1. 新增 `tools/polyld/src/linker_pe.cpp`，与 `linker.cpp` 同风格、同命名空间、同诊断/Trace 机制：
   - `Linker::GeneratePEExecutable()`：把 `output_sections_` 翻译成 `pe_writer::SectionLayout[]`；从 `imports_` 构建 `IATBuilder`；解析入口符号（`mainCRTStartup` / 用户 `--entry`）→ entry RVA；调 `BuildPE32PlusImage`；写文件；填 `stats_.total_output_size`。
   - `Linker::GeneratePEDll()`：`IMAGE_FILE_DLL` + 入口 `_DllMainCRTStartup`；导出表 EAT 由 `exports_`（`__declspec(dllexport)` / 命令行 `/EXPORT:` / `.def` 文件）构建；正确生成 `.edata` 节、`Name Pointer Table` / `Ordinal Table` / `Export Address Table`。

2. `.def` 文件支持：polyld 接受 `--def <file>`，解析 `LIBRARY / EXPORTS` 段；与命令行 `/EXPORT` 合并去重；冲突报错 `polyld-err-E3201`。

3. 重定位翻译层：把 polyld 内部 `RelocationEntry` 翻译为 PE base relocation 集合；所有不可在 PE 上表达的 reloc（如 ELF GOT/PLT 专属类型）转成等价的 IAT 引用，否则抛 `polyld-err-E3210`（绝不静默丢弃）。

4. 测试：
   - `tests/integration/e2e_pe_smoke.cpp`：用 polyc 编译 `samples/01_basic_linking/main.ploy` → 真 `.exe`，`CreateProcessW` 启动并断言退出码；`dumpbin /headers /imports` 解析断言。
   - `tests/integration/e2e_pe_dll_smoke.cpp`：编译一个导出 `int Add(int,int)` 的 `.dll`，宿主测试程序 `LoadLibraryW` + `GetProcAddress` + 调用断言返回值。
   - 非 Windows runner 上仅做"PE 字节合法性"断言（解析头），不执行。

5. 文档：在 `docs/realization/binary_containers_*.md` 追加 "PE 路径细节" 章节（双语）。
6. 版本：`1.5.0-pre.3` → `1.5.0-pre.4`。完成后追加 `--end -done`。

--end -done


2026-04-28-45

[BIN-5] Mach-O 写出器真实化：可执行 + dylib + bundle，覆盖 x86_64 / arm64。

依赖：41 / 42。

1. 现有 `Linker::GenerateMachOExecutable()` 仅写 64-bit header 骨架，需扩展（不破坏旧 API）：
   - 完整 load command 集合：`LC_SEGMENT_64` × N（`__PAGEZERO / __TEXT / __DATA_CONST / __DATA / __LINKEDIT`）、`LC_SYMTAB`、`LC_DYSYMTAB`、`LC_LOAD_DYLINKER`（`/usr/lib/dyld`）、`LC_MAIN`（含 entryoff）、`LC_LOAD_DYLIB`（`libSystem.B.dylib` 等）、`LC_BUILD_VERSION`（含 platform / minos / sdk）、`LC_SOURCE_VERSION`、`LC_UUID`（按节内容 SHA-256 截断 16B）。
   - 重定位：`X86_64_RELOC_*` 与 `ARM64_RELOC_*` 完整翻译表（branch26 / page21 / pageoff12 / got_load / unsigned）。
   - 字符串表 / 符号表 / 间接符号表 全部真实生成；`__LINKEDIT` 段内布局严格按苹果工具链顺序。

2. 新增 `Linker::GenerateMachOBundle()`（`MH_BUNDLE`）作为附加产物形态。`GenerateMachODylib()` 真正生成 `MH_DYLIB` + `LC_ID_DYLIB`，不再转给 executable。

3. 新增 `tools/polyld/src/linker_macho.cpp`，把以上逻辑从 `linker.cpp` 拆出，与 `linker_pe.cpp` 平级。

4. 代码签名：保留可选的 ad-hoc `LC_CODE_SIGNATURE` 占位段（仅生成段表项 + 空 superblob），便于在 macOS 上后续 `codesign --force --sign -` 注入；不引入私钥逻辑。

5. 测试：
   - `tests/integration/e2e_macho_smoke.cpp`：macOS runner 上端到端运行；
   - 其他平台上做 magic / load command 表结构断言（解析自写 reader）；
   - dylib + 加载（macOS）：`dlopen / dlsym` 调用断言。
   - 用 `otool -hlLR` 输出与本地解析对账。

6. 文档：`docs/realization/macho_writer_zh.md` + `_en.md`，含 load command 速查与调试方法。
7. 版本：`1.5.0-pre.4` → `1.5.0-pre.5`。完成后追加 `--end -done`。

--end -done


2026-04-28-46

[BIN-6] Wasm 容器在 polyld 派发链中的一等公民化（wasm32-wasi）。

依赖：41 / 42。

1. 现状：`backends/wasm` 已可输出 wasm bytes，但 polyld 完全不参与；`OutputFormat::kExecutable + container=kWasm` 在新派发表里需要明确路径。

2. 实装 `Linker::GenerateWasmModule()`：
   - 输入：后端产出的 `.wasm` 模块字节（多 section `type / import / func / table / memory / global / export / start / element / code / data / custom`）；
   - polyld 行为：合并多个 `.wasm` 输入（多模块 link，按 wasm-ld 风格），处理 `import` / `export` / `name` custom 节；产出单一 `.wasm`。
   - 仅在 multi-module 输入时启用合并；单模块直通时仍校验头并写出。
   - 对 `wasm32-wasi`：补 `_start` 入口存在性检查；缺失时 `polyld-err-E3320`。

3. 命令行：`polyc --target=wasm32-wasi -o app.wasm`；polyld 透传不变。

4. 测试：
   - `tests/integration/e2e_wasm_smoke.cpp`：用 wasmtime（若 PATH 可用）或自带轻量 runtime（`runtime/wasm`）真跑，断言 stdout / 退出码；
   - 多模块合并测试：两个 `.wasm` 模块互相 `import`/`export`，链接后单模块运行。

5. 文档：`docs/realization/wasm_pipeline_zh.md` + `_en.md`（如已存在则增补"polyld 接入"章节，双语）。
6. 版本：`1.5.0-pre.5` → `1.5.0-pre.6`。完成后追加 `--end -done`。

--end -done


2026-04-28-47

[BIN-7] polyc / driver 透传与文件后缀策略 + 全工具链 triple 一致性。

依赖：41 / 42 / 44 / 45 / 46。

1. `tools/polyc/src/driver.cpp`：
   - 解析 `--target=<triple>` / `--container=<...>` / `--subsystem=<...>` / `--entry=<sym>`；
   - host 自动推导：未指定 `--target` 时使用 `common::HostTriple()`；
   - 透传给 polyld：`-T <triple> --container <...> --subsystem <...> --entry <sym>`；
   - 文件后缀策略（实现 42-3，但放在 driver 收口）。

2. `tools/polyc/src/compilation_pipeline.cpp`：
   - `target_os` / `target_triple` 字段对齐 `common::TargetTriple`；
   - 旧字段保留并由 setter 自动同步，避免破坏既有调用方。

3. polyasm / polyopt / polybench / polyrt：
   - 全部接入 `common::TargetTriple`；输出/输入头里写入 triple 字段，下游工具校验匹配，不匹配 `*-warn-W1101`。

4. 测试：
   - `tests/integration/e2e_triple_propagation.cpp`：在 4 个 host 上跑 6 种 target，端到端校验产物 magic 与 triple 一致。

5. 文档：USER_GUIDE 中英双语补 "Cross compilation" 一节（target triple 表 / 默认行为 / 后缀策略 / 常见错误码）。
6. 版本：`1.5.0-pre.6` → `1.5.0-pre.7`。完成后追加 `--end -done`。

--end -done


2026-04-28-48

[BIN-8] 端到端验证、回归矩阵、CI 集成、文档收口与 1.5.0 发布。

依赖：41 ~ 47。

1. 端到端测试矩阵 `tests/integration/binary_matrix/`：
   - 输入：`samples/01_basic_linking` / `samples/05_polymorphism` / `samples/22_database_access` 等 6 个代表性样例；
   - 目标：`x86_64-pc-windows-msvc / aarch64-pc-windows-msvc / x86_64-unknown-linux-gnu / aarch64-unknown-linux-gnu / x86_64-apple-darwin / aarch64-apple-darwin / wasm32-wasi`；
   - 每格断言：产物 magic、容器结构（解析 header / 节表）、能在对应 runner 上启动并退出码正确（不可用 runner 上仅做静态断言）。

2. CI：
   - GitHub Actions / 本地 `scripts/ci/run_binary_matrix.{ps1,sh}`；
   - 引入 `dumpbin` / `otool` / `readelf` / `wasm-objdump` 的可用性探测，缺失时 fall back 到自带 reader；
   - 矩阵失败时落产物到 `artifacts/binary_matrix/` 便于事后分析。

3. 性能基线：`polybench` 加 `pe_link_time` / `macho_link_time` / `elf_link_time` / `wasm_link_time` 四个 case；与 1.4.x ELF 基线对比，回归阈值 ≤ +15%。

4. 回归：
   - `samples/build_all_samples.{ps1,sh}` 在 Win / Linux / macOS 三宿主上分别确认产物可执行；
   - 旧的"假 .exe (实际 ELF)" 行为在 1.5.0 里**绝不可复现**，添加专门的反向断言测试。

5. 文档收口：
   - `docs/realization/binary_containers_zh.md` + `_en.md` 总文档；
   - 子文档：`pe_writer_*.md` / `macho_writer_*.md` / `wasm_pipeline_*.md`（双语）；
   - `README` 中英双语首屏更新"支持的目标平台 / 容器格式"表；
   - `CHANGELOG_zh.md` + `CHANGELOG_en.md` 追加 1.5.0 章节，列出 41~48 全部对外可见变化（不出现需求管理过程相关字样）。

6. 版本：`1.5.0-pre.7` → `1.5.0`（正式发布）；同步 `VERSION.txt`、`tools/*/CMakeLists.txt` 的版本宏（如有）、安装包脚本 `scripts/package_*.{ps1,sh}` 内嵌版本（如有）。完成后追加 `--end -done`。

--end -done

2026-04-28-49

为支撑后续样例集（demand-04）真正可在 Windows 宿主上做 stdout 字节级比对，
需要把 .ploy 源文件与产出 .exe 之间的运行时-IO 通路打通。该工作横跨
front end / IR / backend / linker / runtime 五层，无法在单条 demand 内一次落地，
现拆分为以下 8 个有界阶段（B1..B8）逐次推进，每阶段独立完成、独立通过回归、
独立追加 --end -done：

- **B1（已完成 v1.5.1）**：pe_writer 支持 `.rdata` 节 + 多 import；新增工厂
  `BuildHelloWorldPE(message)` 产出可直接打印消息并 `ExitProcess(0)` 的
  PE32+；单元 4 case + 集成 1 case 全绿。
- **B2**：`.ploy` 顶层语句新增 `PRINTLN "literal";`（lexer / parser /
  sema / AST round-trip 测试）。
- **B3**：middle IR 新增 `kCallRuntime` opcode + 字符串字面量到 `.rdata`
  的 lowering；IR-printer 测试。
- **B4**：x86_64 backend 把 `kCallRuntime` 译成
  `lea rcx,[rip+disp]; mov rdx,len; call qword ptr [iat]` 字节序列；
  obj 字节级比对测试。
- **B5**：polyld 把用户 `.text` 真实拼到 PE，合成 `_start` 入口
  （call user_main → ExitProcess(0)），重定位 `.rdata` 与 IAT 引用；
  端到端 smoke：用 .ploy 写 PRINTLN 程序跑通。
- **B6**：现有 16 个旧样例每个补一条 PRINTLN 给出真实可验证输出
  + `README_zh.md` + `expected_output.txt`；`build_all_samples.ps1` 雏形通过。
- **B7**：14 个新样例（`17_..30_`）按 demand-04 主题逐一补齐。
- **B8**：`scripts/build_all_samples.{ps1,sh}` + `samples_regression_test.cpp`
  + USER_GUIDE / README / CHANGELOG 双语收口 + 版本跃迁
  （patch 累积后跃 minor）+ 在 demand-04 与 demand-49 末尾同时追加
  `--end -done`。

约束：
- 每个阶段都必须是真实实现，不允许占位（顶端规则 3）。
- 阶段间不得破坏现有测试套；每阶段完成时 `test_linker / integration_tests
  / test_e2e` 必须保持全绿。
- 所有新增源代码注释一律英文（顶端规则 2）；新增文档双语成对（顶端规则 5）。
- 新增源文件 / 文档不得出现与本条目自身相关的字样（顶端规则 10）。
- 阶段完成在本条目对应 `Bx` 行末标注 `[done]`；8 个阶段全部 `[done]`
  时在条目末尾追加 `--end -done`。

阶段进度：
- B1 [done]
- B2 [done]
- B3 [done]
- B4 [done]
- B5 [done]
- B6 [done]
- B7 [done]
- B8 [done]

--end -done


2026-04-29-1

[P0/PE-1 真相校准] `.ploy → .obj → .exe` 机器码通路的端到端贯通审计。

背景（实测复现，2026-04-29）：

1. 用 `polyc audit_hello.ploy --emit-obj=audit_hello.obj --obj-format=coff`
   产出的 `.obj` **不是 COFF**——前 16 字节是 `7F 45 4C 46 …`，即 ELF magic；
   MS `link.exe` 当即报 `LNK1107: 文件无效或损坏: 无法在 0x308 处读取`。
2. 把同一文件交给 `polyld -o audit_hello.exe`，最终 `.exe` 的
   `.text` 节 `SizeOfRawData = 0x0D` —— 仅 13 字节，
   内容为 `48 83 EC 28 31 C9 FF 15 2C 10 00 00 CC` 的 ExitProcess(0) shim。
   用户写下的 `RETURN 42` 在最终镜像里完全不存在；运行返回码恒为 0。

四处叠加缺陷：

- **D1**：`tools/polyc/src/compilation_pipeline.cpp::BuildNativeObjectBinary`
  对 `format == "coff"` 没有分支，落入 `else → ELFBuilder`，写出 ELF 字节、
  仅扩展名贴 `.obj`。
- **D2**：`tools/polyc/src/stage_packaging.cpp::BuildCoff()` 结构上是合法
  COFF（20-byte file header + 40-byte section + 18-byte symbol，`#pragma pack(1)`），
  但 `.ploy` staged pipeline 从未调用它，是事实上的死代码。
- **D3**：`tools/polyld/src/linker.cpp::DetectObjectFormat` 用 `MZ` 识别 COFF；
  `MZ` 是 PE 可执行映像的 DOS 残桩，不是 raw COFF `.obj` 的 magic。真正的
  COFF `.obj` 前两字节是 `Machine` 字段（AMD64=`64 86`，ARM64=`64 AA`，
  i386=`4C 01`，IA64=`00 02`），现状会把合法 COFF `.obj` 判为 `kUnknown`，
  仅靠 `_WIN32` 平台分支里 `LoadCOFF` 兜底（`linker.cpp:1925`）才不会立刻挂掉。
- **D4**：`tools/polyld/src/linker.cpp::GeneratePEExecutable` 始终调用
  `pe::BuildExitZeroPE(user_text)`，该函数把 `AddressOfEntryPoint` 钉在
  shim 第一字节；即便 `output_sections_` 真的合并了用户 `.text`，入口也会
  绕过用户代码、直接进 `ExitProcess(0)`。

换句话说：当前 backend → MC bytes → COFF → linker → PE 这条机器码通路里，
唯一被运行的是 polyld 自带的 13 字节 ExitProcess shim；用户代码从未抵达
最终可执行映像，更未被 CPU 取指。

整改路线（5 个子条目，依次推进）：

- **2026-04-29-2 [PE-2]**：polyld `DetectObjectFormat` 修正 COFF magic 判定，
  改为读 2-byte `Machine` 字段（白名单：AMD64/ARM64/i386/ARM/IA64/UNKNOWN-0），
  增配单元测试 `tests/unit/linker/object_format_detect_test.cpp` 喂入合法
  AMD64 / ARM64 / 错位 / 损坏头部的 fixture，断言不再误判。
- **2026-04-29-3 [PE-3]**：polyc 真正写出合法 COFF。把现有
  `stage_packaging.cpp::BuildCoff()` 抽到 `backends/common` 并实现为
  `backends::COFFBuilder : public backends::ObjectFileBuilder`，与
  ELFBuilder/MachOBuilder 同接口；`BuildNativeObjectBinary` 增加 `coff` 分支；
  删除当前 `else → ELF` 兜底。新增 `tests/unit/backends/coff_builder_test.cpp`
  断言：MS `link.exe` 与 LLVM `lld-link` 都能识别该 `.obj`（在 CI 缺这两个
  工具时退化为 hex layout 校验）。
- **2026-04-29-4 [PE-4]**：polyld 把加载到的 `.text` 真正归并进
  `output_sections_`。当前 ELF/COFF loader 似乎漏写或被 layout pass 丢弃；
  排查 `MergeSections` / `LayoutOutput`，补 `tests/unit/linker/section_merge_test.cpp`
  断言「输入 N 个 `.text` 字节 → 输出 PE `.text` 至少 N 字节、且首字节匹配」。
- **2026-04-29-5 [PE-5]**：`Linker::GeneratePEExecutable` 入口策略修正。
  当 `output_sections_['.text']` 含已解析符号（如 `_start` / `main` / Ploy
  mangled `__ploy_main`）时，`AddressOfEntryPoint` 必须指向该符号在 `.text`
  内的真实 RVA；ExitProcess shim 仅作为 fallback，且必须以 `call user_main`
  → `mov ecx, eax` → `call [ExitProcess]` 的形式包裹用户入口，让用户返回值
  成为进程退出码。补 `pe_writer.h` 新接口 `BuildExeWithUserEntry()`，旧的
  `BuildExitZeroPE` 标记 `[[deprecated]]` 但保留以维系 BIN-3 的 PE writer smoke。
- **2026-04-29-6 [PE-6]**：端到端 smoke。新增
  `tests/integration/ploy_e2e_real_exit_code_test.cpp`：写出
  `FUNC main() -> i32 { RETURN 42; }`、`FUNC main() -> i32 { RETURN 0; }`、
  `FUNC main() -> i32 { RETURN 7; }` 三个 `.ploy`，全流程
  `polyc → .obj → polyld → .exe → CreateProcess → WaitForSingleObject →
  GetExitCodeProcess`，断言退出码与源码字面值精确相等。Linux/macOS 平台
  以 `fork/exec/waitpid` 等价实现；CI 矩阵包含三个目标 OS。

约束（与顶端规则 1 / 2 / 3 / 5 / 10 对齐）：

- 整条 PE-2..PE-6 必须真实实现，不允许占位或空 stub；
- 阶段间不得破坏现有测试套；每阶段完成时
  `test_linker / test_backends / integration_tests / benchmark_tests`
  必须保持全绿；
- 所有新增源代码注释一律英文；新增文档双语成对（en + zh）；
- 新增源文件 / 文档不得出现与本条目自身相关的字样；
- 每子条目完成时在本条目对应 `PE-x` 行末标注 `[done]`；
- 5 个子条目全部 `[done]` 时在本条目末尾追加 `--end -done`，并把
  根 `CMakeLists.txt` 与 `VERSION.txt` 的 patch 号递进；
- 文档同步：双语 `CHANGELOG`、`docs/realization/binary_pipeline_*.md`、
  `docs/specs/object_format_*.md` 必须反映新的容器分发与入口策略。

阶段进度：
- PE-2 [done]
- PE-3 [done]
- PE-4 [done]
- PE-5 [done]
- PE-6 [done]

--end -done



2026-04-29-7

[P0/PE-7 真相校准] 真后端 PRINTLN 端到端合龙：把 IR 层已生成的字符串字面量
全局真实落到 `.rdata` 字节、并在 `lea` 指令处发射针对该全局的重定位，
让 `polyc → polyld → .exe` 这条生产路径首次能把 `PRINTLN "literal";`
所写的字面量字节真实写到进程 stdout。

背景（实测复现，2026-04-29，于 v1.5.8）：

1. 把 `FUNC main() -> i32 { PRINTLN "smoke: ok\r\n"; RETURN 0; }` 喂给
   `polyc smoke.ploy --emit-obj=smoke.obj --obj-format=coff --quiet`，
   `polyld smoke.obj -o smoke.exe`，最终 `smoke.exe` 退出码确为 0
   （PE-2..PE-6 已保证），但 `smoke.exe > out.txt` 捕获到 0 字节 stdout。
2. 把生成的 `smoke.obj` 用脚本拆开后看到：`.text` 含 28 字节，能识别出
   `call rel32` 指向外部符号 `polyrt_println` 的占位重定位；`.data` 节内
   仅有 `\x00\x02\x00msg\x00`（COFF 字符串表对符号名 `msg` 的尾巴），
   完全不是 `MakeStringLiteral` 期望的 `s m o k e : ' ' o k \r \n \x00`。
3. 由于 v1.5.8 的 30 个样例脚本 `scripts/build_all_samples.ps1` 使用上述
   字节比对作为唯一可信判据，整张 `samples_report.json` 全部落到
   `COMPILE_FAIL` 或 `EMPTY_STDOUT`，无任何样例进入 `OK` 桶。

四处叠加缺陷（与 PE-2..PE-6 完全不重合）：

- **D1**：`backends/x86_64/src/asm_printer/emit.cpp::X86Target::EmitObjectCode`
  与 `backends/arm64/src/.../EmitObjectCode` 仅遍历 `Functions/Blocks/Instrs`，
  **从未读取** `ir_ctx_->Globals()`；因此 IR 层 `IRBuilder::MakeStringLiteral`
  挂在 `IRContext::globals_` 上的 `ConstantString("smoke: ok\r\n")` 永远不会
  被任何后端写到 `.rdata`/`.data` 字节流。
- **D2**：同两处后端发射 `lea rcx,[rip+disp32]; mov rdx,len; call qword ptr [iat]`
  时，`disp32` 写为 0 但**没有同时追加**任何 `Relocation{symbol="println.msgN.ptr"}`
  到 `text_sec.relocations`。结果：B5 的 `CollectPolyrtPrintlnSequence`
  pass（`tools/polyld/src/linker.cpp:3012`）的「最近一次 message-load reloc」
  游标永远扑空，链接器即便能识别 `polyrt_println` call 也无法配对消息。
- **D3**：`tools/polyc/src/compilation_pipeline.cpp:797` 的
  `sec.data = !obj.code.empty() ? obj.code : obj.data;` 是「单段二选一」
  写法；即使后端真的产出多节（`.text` + `.rdata` + `.data`），这里也会
  因为 `obj.code` 非空而把 `obj.data` / 后续节静默丢弃。需要改为
  「逐节透传」并保持 `.text → .rdata → .data → .bss` 的稳定排列。
- **D4**：`frontends/ploy/src/lowering/lowering.cpp` 处理顶层语句序列时，
  当 `.ploy` 没有显式 `FUNC main` 而仅含 `PRINTLN`/赋值/调用时，下沉过程
  会把这些语句直接放进 `entry` block 而不追加 `RETURN`，触发 IR 验证器
  报 `block missing terminator: entry`（v1.5.8 实测）。这一缺陷使顶层
  PRINTLN 永远走不到后端，与 D1/D2 联合阻塞了 demand-04 第 5/6 项。

整改路线（5 个子条目，依次推进）：

- **2026-04-29-8 [PE-7-A]**：在 `frontends/ploy/src/lowering/lowering.cpp`
  顶层语句下沉路径补一道「合成 `__ploy_main`」逻辑。当 `.ploy` 文件没有
  显式 `FUNC main(...) -> i32 { ... }` 时，自动用收集到的顶层语句作为函数
  体，插入合成函数 `__ploy_main`：返回类型 `i32`，参数空，末尾追加
  `RETURN i32 0`。`__ploy_main` 同时作为 polyld 期望的入口符号被标记
  global。新增 `tests/unit/frontend/ploy/synthetic_main_test.cpp` 断言：
  仅含 `PRINTLN "x";` 的 `.ploy` 经下沉后产出的 IR 中存在恰好一个
  `Function{name="__ploy_main", ret=i32}`，其 `entry` block 末指令是
  `Return(I32 0)`，且 IR 验证器返回 `kOk`。
- **2026-04-29-9 [PE-7-B]**：在 `backends/x86_64/src/asm_printer/emit.cpp`
  `X86Target::EmitObjectCode()` 函数体末尾、写完 `.text` 之后，新增一段
  「globals 发射」逻辑。遍历 `ir_ctx_->Globals()`：
  - 若全局 `initializer` 为 `ConstantString`，把字节追加到 `.rdata` 节
    （`SectionFlags::kReadOnly | kInitialized`），写一条
    `Symbol{name=global.name, section=".rdata", offset=追加前 size,
    size=string.size()+1, is_global=true, is_function=false}`。
  - 若全局 `initializer` 为 `ConstantGEP(base=<上述全局>, idx={0,0})`
    且名字以 `.ptr` 结尾，把 8 字节 `0x00` 占位追加到 `.rdata`，写一条
    `Symbol{name=global.name, ...}` 与一条
    `Relocation{offset=占位起点, symbol=base 名, type="ABS64", addend=0}`。
  在 `backends/arm64/src/...` 同样位置做对偶实现，relocation type 改为
  `ABS64`（arm64 的 64 位绝对填充语义一致）。新增
  `tests/unit/backends/x86_64/globals_emit_test.cpp` 与
  `tests/unit/backends/arm64/globals_emit_test.cpp`：构造一个含
  `MakeStringLiteral("hi\n")` 的 IRContext，驱动 `EmitObjectCode()`，
  断言：①返回的 `mc.sections` 中存在 `.rdata` 节；②`.rdata` 起始 4 字节
  恰为 `'h','i','\n','\0'`；③`mc.symbols` 中存在 `println.msg0` 与
  `println.msg0.ptr` 两条；④后者所在节有一条 `ABS64` reloc 指向前者。
- **2026-04-29-10 [PE-7-C]**：在两个后端的 `lea reg,[rip+disp32]` 发射点
  （目前 `disp32 = 0` 写死）追加 reloc 发射逻辑。当 `lea` 的源操作数是
  指向某 `GlobalValue` 的 `Operand{kind=GlobalRef}` 时，向当前 `text_sec`
  的 `relocations` 追加 `Relocation{offset=disp32 字节起点, symbol=
  GlobalValue.name（即 `println.msgN.ptr`）, type="REL32", addend=-4}`。
  对应 arm64 后端用 `ADRP/ADD` 发射点同步插入 `PAGE21+PAGEOFF12` 两条
  reloc。新增 `tests/unit/backends/x86_64/printf_lea_reloc_test.cpp`：
  构造一段调用 `polyrt_println` 的 IR，断言 `mc.sections[".text"].relocations`
  中按源代码顺序出现 `(REL32 -> println.msg0.ptr)` 与
  `(REL32 -> polyrt_println, addend=-4)` 的成对模式，且后者的 `offset`
  恰好等于前者 `offset + 11`（lea 指令 7 字节 + mov 指令 7 字节 - 3，
  按当前指令选择确定）。
- **2026-04-29-11 [PE-7-D]**：把 `tools/polyc/src/compilation_pipeline.cpp`
  打包阶段从「单段二选一」改成「逐节透传 + 稳定排序」。改动点：
  - 删除 `sec.data = !obj.code.empty() ? obj.code : obj.data;` 这一处；
  - 引入 helper `AbsorbObjectSections(obj, sections, sec_index)`：遍历
    `obj.sections` 的全部节，按节名查 `sec_index`：若已存在则追加字节
    到末尾并平移后续 reloc 的 `offset`；若不存在则新建 `InternalSection`；
  - 全部输入归并完成后，按 `.text → .rdata → .data → .bss → 其它` 的
    优先级稳定排序 `sections`（同优先级内保留输入顺序）。
  补 `tests/unit/polyc/section_passthrough_test.cpp` 断言：①输入两个
  `obj`，第一个含 `.text(8B) + .rdata(4B)`，第二个含 `.text(6B)`，
  打包后 `sections` 恰为 `[".text"(14B), ".rdata"(4B)]`；②第二个 obj
  在 `.text` 内的 reloc `offset` 全部加 8。
- **2026-04-29-12 [PE-7-E]**：端到端 smoke + 严格门禁切换。
  - 新增 `tests/integration/printf_pipeline_e2e_test.cpp`（Catch2 标签
    `[printf][pe7][integration]`）：写出
    `FUNC main() -> i32 { PRINTLN "alpha\r\n"; PRINTLN "beta\r\n";
    RETURN 0; }`，全流程 `polyc → .obj → polyld → .exe →
    CreateProcess + 重定向 stdout 到管道 → WaitForSingleObject →
    ReadFile`，断言捕获到的 stdout 字节恰为 `alpha\r\nbeta\r\n`。
    Linux/macOS 平台用 `fork/pipe/dup2/exec/waitpid` 等价实现。
  - 把 `scripts/build_all_samples.ps1` 与 `.sh` 增加新开关
    `-RequireMinOk <N>` / `--require-min-ok <N>`：当 `OK` 桶数量小于
    阈值时退出码 1。CI 配置里把现有 30 样例的 `samples_regression_test`
    切换到 `--require-min-ok 1`（首版）→ 后续随真后端实力提升把阈值递进。

约束（与顶端规则 1 / 2 / 3 / 5 / 10 对齐）：

- 整条 PE-7-A..PE-7-E 必须真实实现，不允许占位或空 stub；
- 阶段间不得破坏现有测试套；每阶段完成时
  `test_linker / test_backends / test_frontend_ploy / test_middle /
  integration_tests` 必须保持全绿；
- 所有新增源代码注释一律英文；新增文档双语成对（en + zh）；
- 新增源文件 / 文档不得出现与本条目自身相关的字样；
- 每子条目完成时在本条目对应 `PE-7-x` 行末标注 `[done]`；
- 5 个子条目全部 `[done]` 时在本条目末尾追加 `--end -done`，并把根
  `CMakeLists.txt` 与 `VERSION.txt` 的 patch 号递进；当 `samples_regression_test`
  切到严格门禁通过后，方可在 `2026-04-28-4` 与 `2026-04-28-49` 两条
  demand 末尾同时追加 `--end -done`；
- 文档同步：双语 `CHANGELOG`、`docs/realization/runtime_stdout_pipeline*.md`
  必须新增「PE-7 — 真后端 PRINTLN 合龙」章节，描述上述 D1..D4 与
  S1..S5 的对应关系。

阶段进度：
- PE-7-A [done]
- PE-7-B [done]
- PE-7-C [done]
- PE-7-D [done]
- PE-7-E [done]

--end -done


2026-05-06-1

在 polyld 的 Mach-O 写出器中补齐另两条标准的 `linkedit_data_command`，
让产出的 64 位 arm64 / x86_64 可执行文件的 load-command 序列与 Apple
ld 对零函数表 / 零数据-在-代码段二进制的输出在结构上完全等价。本条
要求只覆盖结构性发射与单元测试，不引入对运行时 execve 的依赖，能够
独立完成。

实现要点：

1. 在 `tools/polyld/include/linker_macho.h` 中新增两个常量：
   `kLcFunctionStarts = 0x00000026`、`kLcDataInCode = 0x00000029`
   （两条均为不带 `LC_REQ_DYLD` 位的标准 linkedit_data 命令）。
2. 在 `tools/polyld/src/linker_macho.cpp` 的 `BuildMachOImage` 中：
   - 在 `cmds_size += 16; ++ncmds; // LC_DYLD_EXPORTS_TRIE` 之后追加
     `cmds_size += 16; ++ncmds; // LC_FUNCTION_STARTS`
     与 `cmds_size += 16; ++ncmds; // LC_DATA_IN_CODE`；
   - 在 LINKEDIT 布局中，紧接 `exports_trie_bytes` 之后构造
     `function_starts_bytes`（单字节 `0x00` 即 ULEB128 终止符，按
     8 字节对齐补零)，并构造 `data_in_code_bytes`（长度 0）。两段
     payload 各自的 `fileoff` 由前一段尾部累加得到，参与
     `linkedit_filesize_raw` 求和，并在 image 字节流中按
     `chained → exports_trie → function_starts → data_in_code →
     nlist → string` 的顺序写入；
   - 在 LC 发射阶段，在 `LC_DYLD_EXPORTS_TRIE` 之后、`LC_SYMTAB`
     之前依次发射两条 16 字节 `linkedit_data_command`，
     `cmd / cmdsize / dataoff / datasize` 字段正确。
3. 由于本条目仅扩展 LINKEDIT 布局，`BuildLinkerSignedSignature` 调用
   不必更改：`code_limit = codesig_fileoff` 自动覆盖新追加的字节，
   page-hash 数与 hash 内容会随 LINKEDIT 增长自然变化；codesign
   `--verify --strict` 仍需返回 `valid on disk`。
4. 新增 `tests/unit/polyld/macho_linkedit_data_emit_test.cpp`
   （Catch2 标签 `[macho][linkedit][polyld]`），构造一个仅包含
   `__TEXT,__text` 的最小 `BuildRequest`，调用 `BuildMachOImage`
   并断言：
   - 通过 `otool`-等价的 in-memory 解析，命令序列中
     `LC_DYLD_CHAINED_FIXUPS → LC_DYLD_EXPORTS_TRIE →
     LC_FUNCTION_STARTS → LC_DATA_IN_CODE → LC_SYMTAB`
     依次出现；
   - `LC_FUNCTION_STARTS.datasize == 8`，且对应文件偏移处的 8
     字节为 `00 00 00 00 00 00 00 00`；
   - `LC_DATA_IN_CODE.datasize == 0`；
   - `LC_FUNCTION_STARTS.dataoff` 等于
     `LC_DYLD_EXPORTS_TRIE.dataoff + LC_DYLD_EXPORTS_TRIE.datasize`，
     `LC_DATA_IN_CODE.dataoff` 等于
     `LC_FUNCTION_STARTS.dataoff + LC_FUNCTION_STARTS.datasize`，
     `LC_SYMTAB.symoff` 等于
     `LC_DATA_IN_CODE.dataoff + LC_DATA_IN_CODE.datasize`；
   - `__LINKEDIT.filesize` 严格等于代码签名 blob 末尾偏移减去
     `__LINKEDIT.fileoff`。
5. 运行回归 `./build/integration_tests "[bin8],[bin7],[samples]"`
   必须保持 8 个用例 / 151 条断言全绿；并执行
   `codesign --verify --strict /tmp/hello`，要求输出包含
   `valid on disk`。
6. 双语 CHANGELOG（`docs/CHANGELOG.md` 与 `docs/CHANGELOG_zh.md`）
   各追加一条 v1.42.5 章节，描述本次新增的两条 LC 与 LINKEDIT 布局
   变化；行文中不得出现与本条目自身相关的字样。
7. 根 `CMakeLists.txt` 的 `project(... VERSION ...)` patch 号由
   1.42.4 递进到 1.42.5；`VERSION.txt` 同步更新。

约束（与顶端规则 1 / 2 / 3 / 4 / 5 / 7 / 8 / 9 / 10 对齐）：

- 必须真实实现两条 LC 的发射与字节注入，不允许仅修改 cmd-size
  计数或仅写注释占位；
- 不得删除或弱化已经存在的 `LC_DYLD_CHAINED_FIXUPS`、
  `LC_DYLD_EXPORTS_TRIE`、`LC_CODE_SIGNATURE` 任何一条；
- 单元测试必须真实读取 `BuildMachOImage` 返回的字节并断言上述六项；
  禁止把断言写成与实现耦合的 mock；
- 全程不得修改 `tools/polyc`、`runtime/`、前端源码；
- 完成时在本条目末尾追加 `--end -done`，并把根 `CMakeLists.txt`
  与 `VERSION.txt` 的 patch 号递进；同步更新双语 CHANGELOG。

--end -done

2026-05-06-2

本条目用于在三平台上把 `2026-04-28-4` 的「样例可真实运行并对齐
`expected_output.txt`」推进到 macOS arm64 / x86_64 这一支。范围只覆盖
Mach-O 写出器 + 必要的最小动态加载支持，不触动前端 / 中端 / 运行时。

实现要点（全部在 `tools/polyld/{include,src}/linker_macho.cpp` 与对应
头文件中完成；需要的 cs_blobs 常量在已存在的匿名命名空间内追加）：

1. 让 `polyld` 产出的 Mach-O 可执行文件在 macOS 26（Tahoe）arm64 host
   上通过 AMFI / CoreTrust / AppleSystemPolicy 三道闸,真正能被
   `execve(2)` 加载并跑到 `LC_MAIN.entryoff` 指向的 `_main`。具体补
   齐：
   - **真实 exports trie**：`LC_DYLD_EXPORTS_TRIE.datasize > 8`，且
     trie 内必须至少包含两个导出符号 `_main`（地址 = `LC_MAIN.entryoff
     + __TEXT.fileoff`，flags = `EXPORT_SYMBOL_FLAGS_KIND_REGULAR`）
     与 `_mh_execute_header`（地址 = 0，flags 同上）。Trie 字节按
     dyld 的 `<mach-o/loader.h>` ULEB128 节点格式编码（terminal_size、
     children_count、edge_string、child_offset 全部就位），整体按 8
     字节对齐。
   - **`LC_FUNCTION_STARTS`**：单函数情况下 payload 为
     `ULEB128(LC_MAIN.entryoff) + 0x00`（终止符），按 8 字节对齐；
     多函数时按地址差分 ULEB128 编码。
   - **`LC_DATA_IN_CODE`**：`datasize = 0`，dataoff 仍占合法槽位。
   - LC 顺序固定为 `... LC_DYLD_CHAINED_FIXUPS → LC_DYLD_EXPORTS_TRIE
     → LC_FUNCTION_STARTS → LC_DATA_IN_CODE → LC_SYMTAB → LC_DYSYMTAB
     → LC_LOAD_DYLINKER → LC_LOAD_DYLIB → LC_UUID → LC_BUILD_VERSION
     → LC_SOURCE_VERSION → LC_MAIN → LC_CODE_SIGNATURE`。
   - LINKEDIT 字节流顺序：`chained_fixups → exports_trie →
     function_starts → data_in_code → nlist → string → code_signature`，
     每段 `dataoff` 严格衔接，`__LINKEDIT.filesize` 等于
     `code_signature` 末尾偏移减去 `__LINKEDIT.fileoff`。
2. 单元测试 `tests/unit/polyld/macho_exports_trie_test.cpp`
   （Catch2 标签 `[macho][exports_trie][polyld]`）：构造一个含
   `__TEXT,__text` 与 `LC_MAIN.entryoff = 0x10` 的 `BuildRequest`，
   调用 `BuildMachOImage`，断言：
   - 解析 `LC_DYLD_EXPORTS_TRIE.datasize >= 24`；
   - 用本仓内手写的 ULEB128 解码器从 trie root 开始遍历，能够枚举
     出且仅枚举出 `_main` 与 `_mh_execute_header` 两个名字；
   - `_main` 解出的 address 等于 `LC_MAIN.entryoff +
     __TEXT.fileoff`，`_mh_execute_header` 解出的 address 等于 0；
   - `LC_FUNCTION_STARTS.datasize >= 2`，前一个字节为
     `ULEB128(LC_MAIN.entryoff)` 的首字节，最后字节为 `0x00`。
3. 集成测试 `tests/integration/macho_exec_smoke_test.cpp`
   （Catch2 标签 `[macho][exec][integration]`）：仅在
   `__APPLE__ && __aarch64__` 编译；测试体内 `polyc` 编译
   `tests/samples/00_minimal/print_then_exit.ploy`（若该样例不存在
   则在本条目内补齐 —— 该样例只做 `PRINTLN "ok"; RETURN 0;`）；
   `polyld` 链接产出 `/tmp/polyld_macho_smoke`；用 `posix_spawn`
   + `waitpid` 启动产物，断言 `WEXITSTATUS == 0` 且管道捕获到
   stdout 等于 `"ok\n"`。
4. `codesign --verify --strict /tmp/polyld_macho_smoke` 输出包含
   `valid on disk`；`spctl --assess` 拒绝结果不影响验收（用户态门，
   非内核 execve 闸）。
5. 双语 CHANGELOG（`docs/CHANGELOG.md` 与 `docs/CHANGELOG_zh.md`）
   各追加 v1.43.0 章节，描述本次新增的 exports trie / function
   starts / data in code 与 smoke 测试；行文中不得出现与本条目
   自身相关的字样。
6. 根 `CMakeLists.txt` 与 `VERSION.txt` 由 `1.42.x` 递进到 `1.43.0`
   （新增 LC + 新可执行能力，按 minor 升级）。

约束：

- 必须真实编码 ULEB128 trie 字节并通过断言反向解码验证；不得用
  写死字节数组占位；
- 不得删除或弱化已存在的 `LC_DYLD_CHAINED_FIXUPS`、
  `LC_CODE_SIGNATURE` 与 linker-signed CodeDirectory 任何一条；
- 不得修改 `tools/polyc/`、`runtime/`、前端源码；
- 不得使用 `codesign -s -` 或外部 `ld` 重签名 / 重链接做"曲线救国"，
  签名必须由 `BuildLinkerSignedSignature` 在写出阶段计算完成；
- 完成时在本条目末尾追加 `--end -done`；同步双语 CHANGELOG +
  根 `CMakeLists.txt` + `VERSION.txt`。

--end -done

2026-05-06-3

本条目把 `2026-04-28-4` 推进到 Linux x86_64 / arm64 这一支，覆盖
ELF 静态可执行的产出与最小运行时验证。范围限定在 `tools/polyld/`
的 ELF 写出路径与 `runtime/` 中已存在的 `polyrt_println` /
`polyrt_exit` 系统调用桩，不触动前端 / 中端。

实现要点：

1. 在 `tools/polyld/src/linker_elf.cpp`（如不存在则按 `linker_macho.cpp`
   的代码组织对偶新建 `linker_elf.{h,cpp}` 并接入 `LinkerDriver`）
   中，发射符合 ELF64 规范的可执行文件：
   - ELF header（`EI_CLASS=ELFCLASS64`、`EI_DATA=ELFDATA2LSB`、
     `e_type=ET_EXEC`、`e_machine=EM_X86_64` 或 `EM_AARCH64`、
     `e_entry` 指向 `_start`，与 LC_MAIN 角色等价）；
   - Program headers：`PT_LOAD(R+X)` 覆盖 `.text`、`PT_LOAD(R+W)`
     覆盖 `.data` / `.bss`、`PT_LOAD(R)` 覆盖 `.rodata`，必要时
     发射 `PT_GNU_STACK(R+W)` 标记非执行栈、`PT_GNU_RELRO`；
   - Section headers：至少包含 `.text` / `.rodata` / `.data` /
     `.bss` / `.shstrtab`，符号表 `.symtab` / `.strtab` 可选但若
     发射必须自洽；
   - 入口符号 `_start`：在 x86_64 上发射 `mov rax, 0x3c; mov rdi,
     <main_ret>; syscall` 风格的 `exit` 桩前调用 `main`；arm64 上
     用 `svc #0` + `x8 = 93` 的对偶序列。这层胶水代码可由 polyld
     在写出阶段直接拼装机器码注入 `.text` 头部。
2. `runtime/polyrt_linux.{c,cpp}` 中提供 `polyrt_println(const char *)`
   与 `polyrt_exit(int)` 的纯系统调用实现（`write(2)` syscall 1、
   `exit_group(2)` syscall 231 / arm64 syscall 94），不依赖 libc。
3. 单元测试 `tests/unit/polyld/elf_image_layout_test.cpp`
   （Catch2 标签 `[elf][polyld]`）：构造最小 `BuildRequest`，调用
   ELF 写出器，断言：
   - `e_ident[0..4] == {0x7f,'E','L','F'}`、`e_class == 2`、
     `e_machine` 与目标 arch 匹配；
   - 至少出现一个 `PT_LOAD(R+X)` 段，其覆盖 `e_entry`；
   - `PT_GNU_STACK` 的 `p_flags == PF_R | PF_W`（非执行栈）；
   - `.text` 节字节流前 16 字节即为 polyld 注入的 `_start` 机器码
     模板（按 arch 给出确切字节数组对照）。
4. 集成测试 `tests/integration/elf_exec_smoke_test.cpp`
   （Catch2 标签 `[elf][exec][integration]`，仅在 `__linux__` 编
   译）：编译 + 链接 `tests/samples/00_minimal/print_then_exit.ploy`
   到 `/tmp/polyld_elf_smoke`，`fork + execve + waitpid` 启动，断言
   `WEXITSTATUS == 0` 且 stdout 管道捕获到 `"ok\n"`。
5. 在 `scripts/ci/` 下新增 `run_linux_smoke.sh`（在 macOS 开发机上
   通过 `docker run --rm -v $PWD:/w -w /w/build ubuntu:24.04 bash
   ./../scripts/ci/run_linux_smoke.sh` 即可触发），脚本内做：
   `apt-get install -y cmake g++` → `cmake -S /w -B /w/build-linux`
   → `cmake --build /w/build-linux --target polyld integration_tests`
   → `ctest -R "elf_exec_smoke|polyld_unit"`。脚本退出码 0 即通过。
6. 双语 CHANGELOG 各追加 v1.44.0 章节，描述 ELF 写出能力与 syscall
   桩；行文中不得出现与本条目自身相关的字样。根 `CMakeLists.txt`
   与 `VERSION.txt` 由 `1.43.x` 递进到 `1.44.0`。

约束：

- ELF 写出必须真实可被 Linux 内核 `execve(2)` 加载；不允许用
  shebang 包装 / 调用外部 `ld` / 调用外部 `gcc` 链接；
- 不得修改 `tools/polyc/`、前端源码；
- `runtime/polyrt_linux.*` 内禁止链接 libc，必须直接写 `syscall`
  指令序列（x86_64 用 `inline asm("syscall")`，arm64 用 `inline
  asm("svc #0")`）；
- 完成时在本条目末尾追加 `--end -done`；同步双语 CHANGELOG +
  根 `CMakeLists.txt` + `VERSION.txt`。

--end -done

2026-05-06-4

本条目把 `2026-04-28-4` 推进到 Windows x86_64 / arm64 这一支，把
现有 PE-7 系列 PRINTLN 真后端能力收口为「样例端到端冒烟」，并补齐
`expected_output.txt` 与 `samples_regression_test` 的最小可达基准。

实现要点：

1. 在 `tests/samples/` 下新增最小可运行样例 `00_minimal/`（与
   `2026-05-06-2` / `2026-05-06-3` 共用同一份源码）：
   - `print_then_exit.ploy`：内容仅 `FUNC main() -> i32 {
     PRINTLN "ok"; RETURN 0; }`；
   - `README.md` / `README_zh.md`（双语，按 `2026-04-28-4` 验收
     标准 1 中的双语要求）；
   - `expected_output.txt`：单行 `ok`（行尾按平台 LF；脚本侧统一
     做 CRLF/LF 归一化后比对）。
2. 在 `tests/samples/` 下既有 30 个样例（`01_..30_`）中，逐一审查
   `expected_output.txt`：
   - 凡当前后端能力可真实跑出确定输出的样例，写出真实的预期输出；
   - 凡当前后端能力不可达的样例，把 `expected_output.txt` 改名为
     `expected_output.skip`，并在样例 `README.md` / `README_zh.md`
     的"运行方式"小节中显式标注「当前版本 skip：依赖 <能力 X>」；
   - `scripts/build_all_samples.{ps1,sh}` 必须把 `*.skip` 视为
     SKIP 桶（既不是 OK 也不是 FAIL），且 SKIP 不计入
     `--require-min-ok` 阈值。
3. `scripts/build_all_samples.ps1` 与 `scripts/build_all_samples.sh`
   补齐 `--require-min-ok N` 开关：当 OK 桶 < N 时退出码 1。脚本
   汇总报告必须区分 `OK / FAIL / SKIP` 三栏，并在末尾打印
   `samples_report.json`（与现有 `build/samples_report.json` 的
   schema 一致）。
4. `tests/integration/samples_regression_test.cpp` 改造：
   - 平台分支：Windows 调 `build_all_samples.ps1 --require-min-ok 1`，
     Linux/macOS 调 `build_all_samples.sh --require-min-ok 1`；
   - 解析 `samples_report.json`，断言至少 `00_minimal/print_then_exit`
     落入 OK 桶；
   - 断言 OK 桶集合与 `samples_report.json.ok` 数组按字母序一致
     （防止脚本与测试对"OK"定义不一致）。
5. CI 矩阵：在 `.github/workflows/ci.yml`（或仓库现有等价 CI 配置
   文件）新增三个 job：`samples-windows-2022`、`samples-ubuntu-24.04`、
   `samples-macos-14`，每个 job 跑 `cmake --build` +
   `ctest -R "samples_regression"`。任一 job 失败即整体失败。
6. 双语 CHANGELOG 各追加 v1.45.0 章节，描述 `00_minimal` 与三平台
   CI 矩阵；行文中不得出现与本条目自身相关的字样。根 `CMakeLists.txt`
   与 `VERSION.txt` 由 `1.44.x` 递进到 `1.45.0`。

约束：

- `expected_output.txt` 内容必须由本机真实运行产物 `redirect stdout`
  生成（脚本里写注释指明生成命令），禁止人工捏造；
- 不得为通过测试而把 SKIP 阈值放宽到 0；`--require-min-ok` 必须
  ≥ 1 且包含 `00_minimal/print_then_exit`；
- 不得删除 `01_..30_` 中任何样例（违反顶端规则 6 的延伸：保留
  教学价值），只允许把 `expected_output.txt` 重命名为 `.skip`；
- 完成时在本条目末尾追加 `--end -done`；同步双语 CHANGELOG +
  根 `CMakeLists.txt` + `VERSION.txt`。

--end --status blocked-by:2026-05-06-2

2026-05-06-5

本条目是 `2026-04-28-4` 的收口闸：当且仅当 `2026-05-06-2`、
`2026-05-06-3`、`2026-05-06-4` 三条同时具备 `--end -done` 标记，
方可执行本条目；本条目本身不引入新功能，只做验收与收口。

实现要点：

1. 在三平台开发机（或 CI 三 job）上同时跑：
   - macOS arm64：`./build/integration_tests
     "[macho][exec][integration],[bin8],[bin7],[samples]"` 全绿；
   - Linux x86_64：`./build-linux/integration_tests
     "[elf][exec][integration],[samples]"` 全绿；
   - Windows x86_64：`build\Release\integration_tests.exe
     "[pe7][integration],[samples]"` 全绿。
2. 三平台上 `ctest -R samples_regression` 退出码均为 0；三份
   `samples_report.json` 中 OK 桶集合相同（脚本侧做集合比对，并
   在 `tests/integration/samples_cross_platform_consistency_test.cpp`
   中断言）。
3. 在 `2026-04-28-4` 与 `2026-04-28-49` 两条 demand 末尾追加
   `--end -done`（追加位置严格紧贴该条目原 `--end` 行后）。
4. 在本条目末尾追加 `--end -done`。
5. 双语 CHANGELOG 各追加 v1.45.1 章节，标题为「样例三平台对齐
   收口」；行文中不得出现与本条目自身相关的字样。根 `CMakeLists.txt`
   与 `VERSION.txt` 由 `1.45.0` 递进到 `1.45.1`。

约束：

- 任一前置条目未 done，本条目不得执行任何 `--end -done` 写入；
- 不得为通过验收而修改 `samples_regression_test` 的断言阈值；
- 不得删除或改名前置条目原有的 `--end` 行；只能在其后追加 ` -done`
  后缀（与本文档既有完成标记习惯一致）；
- 全程不得修改前端 / 中端 / 运行时核心；如发现验收阻塞且确属新
  bug，必须在本文档末尾追加新条目（编号 `2026-05-06-6` 起递增）
  描述并独立修复，禁止在本条目内私自扩张范围。

--end

