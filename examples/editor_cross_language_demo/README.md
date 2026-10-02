# Cross-language editor fixture

Open `order_flow.poly`. The C++, Rust, Python, and Go sources live in the same
module folders that the compiler resolves. Select a foreign `CALL` target to
show its original documentation and source preview inside the editor. The
context menu provides **Go to Function Definition** and **Show Source and
Documentation Inline**. F12 opens the selected definition. Escape closes the
preview. Overloaded or ambiguous source functions produce a candidate chooser.

Use **Code**, **Split**, and **Flow** in the source header to resize the source /
function-flow workspace. The graph labels quantities and parameter transfers;
right-click a module call to open its source definition.

Repeatable offscreen UI verification (use your platform's `polyui` binary):

```sh
build-release/polyui.app/Contents/MacOS/polyui \
  --headless --ui-smoke \
  --folder examples/editor_cross_language_demo \
  --file examples/editor_cross_language_demo/order_flow.poly \
  --view split --peek subtotal \
  --screenshot /tmp/polyui-order-flow.png
```

This command checks the visible documentation and source preview, invokes the
actual right-click menu action, and checks the opened file, line, and column.
It writes the workspace screenshot and a separate `.context.png` menu image.
The fixture documents editor and graph behavior; it is not a compiler
performance workload.
