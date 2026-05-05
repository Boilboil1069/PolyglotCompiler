# SCM 进阶：diff / blame / merge resolver（demand 2026-04-28-27）

## 目标

把源代码管理工作流提升到一流 IDE 水准：

* **SCM 提供者抽象**，让 Git、Mercurial、Subversion 共用一套视图模型；
* **Diff 引擎**：并排 / 行内两种显示，行块级 stage / unstage，与编辑器共存
  （diff 区域内仍可悬浮 / 跳转）；
* **Blame**：边栏与悬浮；
* **合并冲突解决器**：一键 Accept current / incoming / both 加自由编辑。

## 组件

| 关注点          | 文件                                                                                              | 职责                                                                          |
|-----------------|---------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| 提供者接口      | [`tools/ui/common/scm/scm_provider.{h,cpp}`](../../tools/ui/common/scm/scm_provider.h)            | `ScmProvider` + `InMemoryScmProvider` 测试替身。                              |
| Git 后端        | [`tools/ui/common/scm/git_provider.{h,cpp}`](../../tools/ui/common/scm/git_provider.h)            | `GitProvider` + porcelain / blame / log 解析器。                              |
| Diff 引擎       | [`tools/ui/common/scm/diff_engine.{h,cpp}`](../../tools/ui/common/scm/diff_engine.h)              | `DiffLines`、`BuildHunks`、`ApplyHunk`、`RevertHunk`。                        |
| 合并解决器      | [`tools/ui/common/scm/merge_resolver.{h,cpp}`](../../tools/ui/common/scm/merge_resolver.h)        | `ParseMergeConflicts`、`ResolveConflicts`、自定义结果。                       |

## 主要流水线

### Diff 视图

```
"与 HEAD 比较" ──► provider.Diff("HEAD", "")
                          │
                          ▼
                   FileDiff[]（解析后的 unified diff）
                          │
                          ▼
              并排 / 行内渲染器
                          │
                          ▼
   "Stage hunk" ──► provider.Stage(file)
   "Revert hunk" ──► RevertHunk(text, hunk) → 写回
```

### Blame

```
打开文件 ──► provider.Blame(path)
                  │
                  ▼
           BlameEntry[]（commit_id、short_id、author、date、summary）
                  │
                  ▼
         边栏显示 short_id + author
         悬浮气泡展示完整提交信息；"打开提交" → log 面板
```

### 合并解决器

```
含 `<<<<<<<` 标记的文件
        │
        ▼
ParseMergeConflicts(text) → MergeConflict[]
        │                      （current_label、incoming_label，
        │                        current / base / incoming 三段）
        ▼
三向视图渲染
        │
        ▼
ResolveConflicts(text, choice, custom)
        │
        ▼
写回，文件重新加载
```

## 测试

* [`tests/unit/polyui/diff_engine_test.cpp`](../../tests/unit/polyui/diff_engine_test.cpp)
* [`tests/unit/polyui/merge_resolver_test.cpp`](../../tests/unit/polyui/merge_resolver_test.cpp)
* [`tests/unit/polyui/scm_provider_test.cpp`](../../tests/unit/polyui/scm_provider_test.cpp)
* [`tests/unit/polyui/git_provider_test.cpp`](../../tests/unit/polyui/git_provider_test.cpp)
