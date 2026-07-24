# 客户留存仪表盘（HTML 前端）

这是一个可以直接用浏览器打开的真实跨语言前端示例。它没有 npm、打包器、CDN 或外部语言编译器；运行一次本地 PolyglotCompiler 后，打开 [`index.html`](index.html) 就能使用完整仪表盘。

```text
Python 遥测生成
        ↓
Java 活跃信号
        ↓
JavaScript 留存健康分
        ↓
Poly 分群、断言与 checksum
        ↓
data/generated_accounts.js
        ↓
HTML / CSS / 浏览器 JavaScript 仪表盘
```

Python、Java 与留存策略 JavaScript 都由 PolyglotCompiler 自带的 lexer、parser、语义分析和 IR lowering 编译进同一个原生程序。浏览器展示层是零构建静态资源：`app.js` 会经过 PolyglotCompiler JavaScript 前端的 `--check`，随后由浏览器执行 DOM 交互；HTML/CSS 本身不是编译器输入。

## 运行前端

先在仓库根目录构建好 `polyc` 与 `polyld`，然后：

```bash
cd examples/customer_retention
./run.sh
```

成功后会输出：

```text
customer_retention: rows=5 healthy=1 watch=1 risk=3 checksum=45
customer_retention: frontend=index.html data=data/generated_accounts.js
```

现在直接双击或在浏览器中打开 `examples/customer_retention/index.html`。页面使用 classic deferred scripts，不使用 `fetch()` 或 ES modules，因此 `file://` 模式即可运行，不需要启动 HTTP 服务。

页面包括：

- 账户总数、平均健康分、高风险账户和平均未登录天数；
- 健康度环形分布图与逐账户信号强度；
- 可搜索、筛选、排序的留存账户表；
- 点击账户后显示 Python → Java → JavaScript 的完整评分轨迹；
- 浏览器侧复用同一留存策略，核对 5 条评分与 checksum `45`。

## 完整验证

```bash
./test.sh
```

测试会在 `PATH=/nonexistent` 的编译环境中完成以下审计：

- 只用仓库内构建出的 `polyc` 与 `polyld` 构建原生程序；
- 精确核对五条 CSV 快照和运行时生成的浏览器数据；
- 检查 Python、Java、JavaScript 的对象、描述符与最终符号；
- 用 PolyglotCompiler JavaScript 前端检查 `generated_accounts.js`、`retention_policy.js` 和 `app.js`；
- 检查脚本加载顺序，并禁止模块加载、网络请求和外部 URL。

## 业务规则

- Python 根据行号生成会话数、购买数、投诉数和距上次登录天数。
- Java 计算 `sessions × 2 + purchases × 5 - complaints × 4`。
- JavaScript 再扣除 `daysSinceLogin × 2`，得到留存健康分。
- Poly 将分数分为健康（`>= 30`）、关注（`>= 10`）和风险（其余）。

五个账户最终得到 1 个健康、1 个关注、3 个风险账户，健康分 checksum 为 `45`。`data/generated_accounts.csv` 是人类可读的基准快照；`data/generated_accounts.js` 则由编译后的原生程序每次重新生成，页面读取的是后者。

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `index.html` | 可直接打开的语义化 HTML 页面 |
| `styles.css` | 响应式仪表盘、图表和抽屉样式 |
| `app.js` | 渲染、搜索、筛选、排序、详情与浏览器侧校验 |
| `customer_retention.poly` | 跨语言编排、断言、分群及浏览器数据生成 |
| `python/telemetry_data.py` | 确定性生成五行遥测数据 |
| `java/SignalRules.java` | Java static 方法计算活跃信号 |
| `javascript/retention_policy.js` | 同时供原生编译和浏览器复用的留存策略 |
| `data/generated_accounts.csv` | 可审计的基准数据快照 |
| `data/generated_accounts.js` | 原生程序生成的浏览器数据 |
| `run.sh` | 隔离工具链、构建、执行并生成页面数据 |
| `test.sh` | 原生链路、页面资产与前端语法审计 |

## 当前边界

第一版使用有符号 64 位整数，保证跨语言 ABI 可预测；浏览器数据仍在安全整数范围内。原生文件写入运行时目前面向 x86_64 macOS/Linux。Java 本地导入还要求模块文件名与承载 static 方法的类名一致，本例均为 `SignalRules`。后续可以继续加入运行时 CSV 读取、更多账户、趋势时间线和真实操作动作。
