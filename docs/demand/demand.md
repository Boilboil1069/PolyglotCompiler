这个文档是需求文档，请根据日期次数分割线进行实现。

要求:

1.在每次回答时都要有称呼MC；
2.代码中的注释使用英文；
3.不允许最小实现/占位等；
4.审查全项目，实现的代码要与全项目的风格符合；
5.生成的文档要中英双语两份文档;
6.不允许删库操作。

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

--end

2026-02-20-9

1.帮我添加java的支持，需要支持java8、java17，java21，java23的功能。
2.帮我添加.net的支持，需要支持.net6、7、8、9

--end