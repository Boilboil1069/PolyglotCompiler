# 遥测、反馈与崩溃报告——隐私声明

**遥测、反馈与崩溃上传默认关闭。** 在你显式同意之前，任何数据
都不会离开本机；你可以随时撤回同意。

## 同意

[`ConsentManager`](../../tools/ui/common/telemetry/telemetry.h)
有三种状态：`unknown`（未询问）、`declined`（拒绝）、`granted`
（同意）。只有 `granted` 允许收集与上传。`Revoke()` 把状态回到
`declined`——已缓冲的事件保留供你查看，但永远不会上传。

## 字段白名单

`FieldAllowList` 是 IDE 能附加到遥测事件的字段显式集合。不在
名单内的字段会在事件进入本地预览缓冲之前 **被剥离**，因此永远
不可能被上传。每个新事件 id 都必须在注册时声明自己的字段白名
单。

## 本地预览

`TelemetryBuffer` 是 IDE 启动以来所有已记录事件的有界内存日志。
打开 *设置 → 隐私 → 遥测预览* 即可审阅将要上传的原始事件。

## 崩溃报告

`CrashReportStore` 始终先把报告落到本地磁盘，绝不直接上网。每
份报告包含版本、平台、信号名、符号化堆栈以及可选的用户备注。
上传是独立闸门（`MarkUploaded`），仅在显式确认后由 IDE 调用。
待处理报告本地可审；你可以随时通过 `Remove(id)` 删除。

## 我们永不收集

* 源代码、文件内容、项目内文件路径。
* 用户名、机器名、IP 地址。
* 任何不在事件白名单内的字段。

## 审计

遥测行为由单元测试完整覆盖：
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp)。
测试确认默认关闭、非法字段被剥离、撤回同意后冻结上传、崩溃
报告在显式确认前保持 pending。
