# Foreign value transfer fixture

Open `flow.poly` in the IDE's Flow view. It imports real C++ and Python sources.

Expected graph:

- `cpp::source::read` produces `return: i32` (from the C++ signature).
- The directed wire enters `python::calibration::scale` at `value: f64`.
- The wire reads `i32 → f64` and its inspector identifies an implicit numeric conversion.
- `scale` produces `return: f64`, which feeds `calibrated_sample · result`.
- Right-click either call card and choose **Go to Definition** to open the foreign source.

This fixture checks static signature and dependency visualization. It does not
assert that the cross-language runtime executes the conversion. Runtime values
are not fabricated or displayed as measurements.

`test_topology_ui "[snapshot]"` checks this graph and a graph with repeated local
calls. Set `QT_QPA_PLATFORM=offscreen` and `POLY_TOPOLOGY_SNAPSHOT_DIR` to a directory
to save the two rendered screenshots.
