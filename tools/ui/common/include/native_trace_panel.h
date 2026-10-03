#pragma once

#include <QByteArray>
#include <QWidget>

#include "tools/ui/common/runtime/native_call_trace.h"

class QCheckBox;
class QLabel;
class QTimer;
class QSplitter;
class QResizeEvent;
class QTreeWidget;

namespace polyglot::tools::ui {

class NativeTracePanel : public QWidget {
  Q_OBJECT
public:
  explicit NativeTracePanel(QWidget *parent = nullptr);
  bool BeginSession(const QString &trace_file, bool follow = true);
  void FinishSession();
  void ClearSession();
  void Poll();
  const runtime::NativeCallTrace &Trace() const { return trace_; }
  const runtime::NativeTraceSample *SelectedSample() const;
  bool OverlayEnabled() const;
  QString Error() const { return error_; }
  QString TraceFile() const { return trace_file_; }
  bool SelectSample(std::size_t index);

protected:
  void resizeEvent(QResizeEvent *event) override;
signals:
  void SampleSelected();
  void OverlayChanged(bool enabled);
  void TraceError(const QString &message);

private:
  void Fail(const QString &message);
  void UpdateSelection();
  void UpdateRows(std::size_t previous_count);
  runtime::NativeCallTrace trace_;
  QString trace_file_, error_;
  qint64 offset_{0};
  QByteArray pending_;
  bool following_{false};
  bool saw_file_{false};
  QTimer *timer_{};
  QCheckBox *overlay_{};
  QLabel *status_{};
  QTreeWidget *samples_{};
  QTreeWidget *values_{};
  QSplitter *values_split_{};
};

} // namespace polyglot::tools::ui
