# IDE Shell Tutorial — Welcome, Notifications, Status Bar, Bookmarks, TODO

This tutorial walks through the IDE shell features: the welcome
page, notification centre, customisable status bar, recent list,
session restore, bookmarks and TODO index.

## 1. Welcome page

When PolyUI starts, the welcome page lists your recent
workspaces (deduplicated by path, newest first), tutorial and
sample entries, and what's-new tips for the current version.
Untick *Show on startup* to hide it; click the pin icon to keep
it as a permanent tab even after you open another file.

The page round-trips to JSON via `WelcomePage::Serialize`, so
your selection of tutorials, samples and tips persists across
restarts.

## 2. Notification centre

Every long-lived message lives in the notification centre.
Notifications carry a severity (`info`, `warning`, `error`,
`progress`), a title, a body, the source subsystem and any
number of action buttons. The status-bar bell shows the unread
count.

* Click an entry to mark it read.
* *Dismiss* removes it from the active list while still keeping
  it for `List(include_dismissed=true)` audits.
* *Do not disturb* suppresses `info` and `progress` posts;
  warnings and errors always go through.

## 3. Customisable status bar

The shell seeds nine built-in slots: `branch`, `problems`,
`language`, `language_server`, `encoding`, `eol`, `indent`,
`package_manager`, `profiler`. Right-click any slot to show or
hide it; drag a slot between the left and right zones (the
`priority` field decides the in-zone order). Extensions register
their own slots through `StatusBar::Register`.

## 4. Recent files / workspaces

`Ctrl+R` opens the recent workspaces palette and `Ctrl+E` opens
the recent files palette. Both share the same `RecentList`
behaviour: re-opening an entry promotes it to the top, pinning
floats it above unpinned entries, and the list is trimmed to a
configurable capacity while keeping every pinned entry.

## 5. Session restore

When session restore is enabled (the default), PolyUI saves the
split-pane layout, every open tab with cursor / scroll / fold
state, panel sizes and visibility, the active debug
configuration with its watches and open views, and any extras
that other features stashed in the session. Restart the IDE and
everything comes back exactly where you left it. Disable
restore in *Settings → Workbench → Session*.

## 6. Bookmarks

`Ctrl+Alt+K` toggles a bookmark on the current line. The
bookmark panel shows every bookmark across the workspace; each
entry can be relabelled and recoloured. `BookmarkStore::Toggle`
returns the new bookmark when the line was empty and `nullopt`
when it removed an existing one, so a single shortcut handles
both add and remove.

## 7. TODO / FIXME index

The background scanner reindexes any file you save against a
configurable keyword set (default `TODO`, `FIXME`; extend with
`XXX`, `HACK`, ...). The scanner enforces word boundaries so
`TODOMARKER` does not match. The summary panel groups hits by
keyword and shows aggregated counts (`CountsByKeyword`).
