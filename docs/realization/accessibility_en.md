# Accessibility

PolyUI's accessibility model targets keyboard-only operation,
mainstream screen readers (NVDA, JAWS, VoiceOver, Orca) and
visual aids (high contrast, large font, reduced motion).

## Focus order

[`FocusOrder`](../../tools/ui/common/a11y/accessibility.h)
maintains a linear keyboard tab chain.  Widgets are sorted by
`tab_index` (ascending), with the id used as a tie-break so the
order is deterministic across runs.  `Next` and `Previous` wrap
around and skip widgets whose `focusable` flag is `false`, so
disabling a control never breaks the chain.

Every focusable widget has a `role` (`button`, `textbox`, `list`,
...) and an accessible `label`.  The Qt layer maps these to the
platform accessibility bridge — UIA on Windows, AT-SPI on Linux,
NSAccessibility on macOS.

## Screen reader announcements

`ScreenReaderQueue::Post` accepts polite or assertive
announcements.  Drains return assertive items first (in FIFO
order) followed by polite items, mirroring the WAI-ARIA live-
region model.  The Qt layer feeds drained items to the platform
bridge so NVDA / JAWS / VoiceOver / Orca can pick them up.

## Visual profile

`AccessibilityProfile` aggregates the visual switches:

* `high_contrast` — selects a high-contrast theme.
* `large_font` + `font_scale_percent` — clamped to 80–300 %.
* `reduce_motion` — disables transitions and animations.
* `preferred_theme` — optional explicit theme override.

The profile round-trips through `SerializeProfile` /
`DeserializeProfile` so the user's choices persist across
restarts.

## Manual testing checklist

* Tab through every dialog with the keyboard alone.
* Verify NVDA / JAWS (Windows), VoiceOver (macOS) and Orca
  (Linux) announce the focused widget's role and label.
* Toggle high-contrast and large-font in settings; check that
  every panel remains usable.
* Toggle reduced motion; confirm the IDE no longer animates
  panel transitions.

Unit coverage:
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp).
