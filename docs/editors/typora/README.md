# Typora Poly / Ploy syntax highlighting

This package adds a real CodeMirror 5 lexer for Poly/Ploy code fences. It does
not merely recolour an existing language: keywords, types, contextual
keywords, declarations, attributes, comments, numbers, operators, regular/raw/
template/multiline strings, indentation, and brace folding are classified from
the current compiler grammar.

本目录为 Typora 提供真正的 CodeMirror 5 Poly/Ploy 词法高亮，而不是把另一种
语言换个颜色。规则依据当前编译器语法，覆盖关键字、类型、上下文关键字、
声明、属性、注释、数字、运算符、普通/原始/模板/多行字符串、缩进与大括号
折叠。

The compiler's canonical language name and source extension are **Ploy** and
`.ploy`. Both Markdown labels are supported deliberately:

编译器中的规范语言名与扩展名是 **Ploy** 和 `.ploy`。为了兼容用户习惯与项目
现有文档，下面两种 Markdown 标签都会使用完全相同的规则：

````markdown
```poly
LET value: i32 = 42;
```

```ploy
LET value: i32 = 42;
```
````

## Package contents / 文件说明

| File | Purpose / 用途 |
| --- | --- |
| `poly.js` | Self-contained CodeMirror 5 mode; registers `poly`, `ploy`, `text/x-poly`, and `text/x-ploy` / 自包含高亮模式与别名 |
| `install_typora.py` | Checks, installs, or surgically uninstalls the Typora integration / 检查、安装、精确卸载 Typora 集成 |
| `poly.user.css` | Optional consistent light/dark token palette / 可选的明暗主题配色 |
| `examples/poly-highlight-demo.md` | Visual acceptance document for both fence names / 两种代码块名称的视觉验收文件 |
| `tests/test_poly_mode.js` | Dependency-free lexer regression test / 无第三方依赖的词法回归测试 |

## Why an installer is needed / 为什么需要安装器

Typora uses CodeMirror for fenced-code highlighting, but its official language
support is a bundled list. The official guidance for a missing language is to
have the language added to CodeMirror and then request Typora support; Typora
does not document a user-level directory for loading an arbitrary mode. See
[Language Support in Code Fences](https://support.typora.io/Code-Fences-Language-Support/)
and [Code Block Styles/Themes](https://support.typora.io/Code-Block-Styles/).

Typora 使用 CodeMirror 为代码块高亮，但官方提供的是随应用打包的语言列表。
官方对未收录语言的建议是先进入 CodeMirror、再申请 Typora 收录，并未提供可从
用户目录加载任意 mode 的正式接口。因此，CSS 只能改变已有 token 的颜色，无法
单独识别 Ploy 词法；要让 language tag `poly` 与 `ploy` 真正生效，必须同时注册
CodeMirror mode 和 Typora 的语言名称映射。

`install_typora.py` therefore makes two minimal changes inside a selected
Typora installation:

因此安装器只在所选 Typora 安装中进行两处最小注入：

1. add `poly` and `ploy` to Typora's language-name mapping and autocomplete;
2. append the marked, self-contained `poly.js` mode to Typora's CodeMirror mode bundle.

安装器先验证两个已知锚点，任何布局不匹配都会在写入前失败。每个原文件都会
生成带 SHA-256 前缀的完整备份；重复安装不重复写入，卸载只移除本工具带标记的
内容，不会恢复旧备份覆盖后续修改。

The integration has been tested against an untouched copy of Typora 1.13.8's
macOS resources. Application updates may replace bundled resources; after an
update, run `--check` and reinstall. If Typora changes its internal layout, the
installer stops rather than guessing.

当前集成已在 Typora 1.13.8 的 macOS 资源副本上通过安装、幂等重装、检查与逐
字节卸载还原测试。Typora 更新可能覆盖应用资源；更新后请重新检查并安装。若
内部结构发生变化，安装器会拒绝猜测式修改。

## Recommended macOS installation / 推荐的 macOS 安装方式

Quit Typora first. The safest workflow patches a separate application copy,
leaving `/Applications/Typora.app` untouched:

先完全退出 Typora。最安全的方式是修改独立副本，保留
`/Applications/Typora.app` 原件不变：

```bash
mkdir -p "$HOME/Applications"
ditto /Applications/Typora.app "$HOME/Applications/Typora-Poly.app"
python3 docs/editors/typora/install_typora.py \
  --typora-root "$HOME/Applications/Typora-Poly.app" \
  --install
codesign --force --deep --sign - "$HOME/Applications/Typora-Poly.app"
```

Open `~/Applications/Typora-Poly.app`, then open
`docs/editors/typora/examples/poly-highlight-demo.md`. The language selector should
offer both `poly` and `ploy`, and both blocks should be coloured.

打开 `~/Applications/Typora-Poly.app`，再打开
`docs/editors/typora/examples/poly-highlight-demo.md`。代码块语言选择器中应出现
`poly` 和 `ploy`，两种代码块都应着色。

> Modifying an application bundle invalidates its vendor signature. The
> `codesign` command above gives only the copied app an ad-hoc local signature;
> it does not preserve or impersonate Typora's vendor signature. Keep the
> original app for trusted updates and rollback.

> 修改应用包会使厂商签名失效。上面的 `codesign` 只给副本添加本机 ad-hoc
> 签名，不会保留或冒充 Typora 厂商签名。应保留原应用用于可信更新和回滚。

If you intentionally want to patch the installed application in place, first
inspect its state, then pass its path explicitly. Permissions may require
running the install command from an administrator-owned shell:

若明确希望原地修改已安装应用，应先检查状态，再显式传入路径；系统权限可能
要求从具有管理员写权限的终端运行安装命令：

```bash
python3 docs/editors/typora/install_typora.py \
  --typora-root /Applications/Typora.app \
  --check
python3 docs/editors/typora/install_typora.py \
  --typora-root /Applications/Typora.app \
  --install
```

## Windows and Linux / Windows 与 Linux

The installer accepts either the `TypeMark` resource directory or a Typora
installation root. It checks common paths automatically, but an explicit path
is more reliable for portable or per-user installations:

安装器既可接收 `TypeMark` 资源目录，也可接收 Typora 安装根目录。它会检查常见
路径，但便携版或用户级安装最好显式指定：

```bash
python3 docs/editors/typora/install_typora.py --check
python3 docs/editors/typora/install_typora.py \
  --typora-root "/path/to/Typora/resources/TypeMark" \
  --install
```

Restart every Typora window after installation. If the application package is
managed by the OS/package manager, prefer a copied installation because an
upgrade will replace the injected bundle.

安装后必须重启所有 Typora 窗口。如果应用由系统或包管理器维护，推荐使用副本，
因为升级会替换已注入的 bundle。

## Optional colours / 可选配色

`poly.js` emits standard CodeMirror classes, so Typora's current theme already
provides colours. For a consistent Poly palette, open Typora's theme folder via
**Preferences → Appearance → Open Theme Folder**, then copy the contents of
`poly.user.css` into `base.user.css` or the active `[theme].user.css`. Restart
Typora after saving.

`poly.js` 只输出标准 CodeMirror class，因此当前 Typora 主题无需额外 CSS 也能
着色。若希望不同主题使用统一的 Poly 配色，请通过 **偏好设置 → 外观 → 打开主题
文件夹**，把 `poly.user.css` 的内容复制到 `base.user.css` 或当前主题对应的
`[theme].user.css`，保存后重启 Typora。

CSS is optional and cannot replace installing the JavaScript mode: it styles
tokens only after the mode has recognised them.

CSS 只是可选配色，不能替代 JavaScript mode；只有 mode 完成 token 识别后，CSS
才有内容可以着色。

## Verification / 验证

Run the lexer regression test:

运行词法回归测试：

```bash
node docs/editors/typora/tests/test_poly_mode.js
```

Expected output / 预期输出：

```text
poly mode: 131 tokens checked; aliases poly/ploy ready
```

Check an installed copy without modifying it:

只检查某个应用副本，不修改文件：

```bash
python3 docs/editors/typora/install_typora.py \
  --typora-root "$HOME/Applications/Typora-Poly.app" \
  --check
```

Expected ready state / 安装完成后的预期状态：

```text
language aliases: installed
CodeMirror mode:  installed
state: ready for ```poly and ```ploy
```

Finally, visually inspect `examples/poly-highlight-demo.md`. This catches theme
CSS issues that a token test cannot see.

最后用 Typora 目视检查 `examples/poly-highlight-demo.md`，以发现 token 测试无法
覆盖的主题 CSS 问题。

## Uninstall and rollback / 卸载与回滚

Quit Typora, then remove only the marked mode and alias injections:

退出 Typora，然后只移除带标记的 mode 和别名注入：

```bash
python3 docs/editors/typora/install_typora.py \
  --typora-root "$HOME/Applications/Typora-Poly.app" \
  --uninstall
```

Content-addressed `*.poly-highlight-backup-*` files are intentionally retained
for audit and manual recovery. The uninstaller does not delete them and does not
overwrite newer Typora resources with an older backup.

内容寻址的 `*.poly-highlight-backup-*` 文件会保留用于审计和手工恢复。卸载器
不会删除备份，也不会用旧备份覆盖较新的 Typora 资源。

## Rule maintenance / 规则维护

The authoritative keyword set is
`frontends/ploy/src/lexer/lexer.cpp::kCanonicalKeywords`. When the language
changes, update these together:

关键字的权威来源是
`frontends/ploy/src/lexer/lexer.cpp::kCanonicalKeywords`。语言变化时必须同步：

1. `poly.js` keyword/type/atom groups;
2. `tests/test_poly_mode.js` representative assertions;
3. `examples/poly-highlight-demo.md` when a new lexical family is introduced;
4. the compiler's language specification and complete tutorial.

`CLASS`, `HANDLE`, and `ATTR` remain contextual parser keywords. Highlighting
them globally is a deliberate editor affordance, not a claim that they are
legal identifiers in every grammar position.

`CLASS`、`HANDLE`、`ATTR` 仍是 parser 的上下文关键字。编辑器全局着色只是为了
提高可读性，并不表示它们在所有语法位置都合法。

## Reuse outside Typora / 在 Typora 之外复用

`poly.js` is a standalone CodeMirror 5 mode. In another CodeMirror 5 host,
load the file after CodeMirror and select `poly`, `ploy`, `text/x-poly`, or
`text/x-ploy` as the mode. The Typora installer is not needed in hosts that
already expose a normal custom-mode loader.

`poly.js` 也是独立的 CodeMirror 5 mode。在其他 CodeMirror 5 宿主中，只需在
CodeMirror 后加载该文件，并选择 `poly`、`ploy`、`text/x-poly` 或
`text/x-ploy`；如果宿主本身支持自定义 mode，就不需要 Typora 安装器。
