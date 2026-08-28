/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plot_docker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QLayout>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr auto kAppliedLeftMargin = "PlotJuggler::canvas_alignment_left";
constexpr auto kAppliedRightMargin = "PlotJuggler::canvas_alignment_right";

struct PlotGeometry
{
  PlotWidget* plot = nullptr;
  QRect outer;
  QRect canvas;
  int natural_canvas_left = 0;
  int natural_canvas_right = 0;
  int old_left_margin = 0;
  int old_right_margin = 0;
};

bool sameVerticalColumn(const PlotGeometry& lhs, const PlotGeometry& rhs)
{
  const int reference_width = std::max(lhs.outer.width(), rhs.outer.width());
  const int center_tolerance = std::max(8, reference_width / 50);   // 2%
  const int width_tolerance = std::max(12, reference_width / 20);  // 5%

  return std::abs(lhs.outer.center().x() - rhs.outer.center().x()) <= center_tolerance &&
         std::abs(lhs.outer.width() - rhs.outer.width()) <= width_tolerance;
}

class PlotCanvasAlignmentFix : public QObject
{
public:
  explicit PlotCanvasAlignmentFix(QApplication* app) : QObject(app), _app(app)
  {
    _app->installEventFilter(this);
    scheduleAlignment();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (!_aligning && watched && watched->isWidgetType())
    {
      switch (event->type())
      {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::LayoutRequest:
        case QEvent::PolishRequest:
        case QEvent::FontChange:
        case QEvent::StyleChange:
          scheduleAlignment();
          break;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void scheduleAlignment()
  {
    if (_pending || _aligning)
    {
      return;
    }
    _pending = true;
    QTimer::singleShot(0, this, [this]() {
      _pending = false;
      alignAllDockers();
    });
  }

  void alignAllDockers()
  {
    if (_aligning)
    {
      return;
    }
    _aligning = true;

    QSet<PlotDocker*> dockers;
    for (QWidget* top_level : QApplication::topLevelWidgets())
    {
      if (auto* docker = qobject_cast<PlotDocker*>(top_level))
      {
        dockers.insert(docker);
      }
      const auto children = top_level->findChildren<PlotDocker*>(QString(), Qt::FindChildrenRecursively);
      for (auto* docker : children)
      {
        dockers.insert(docker);
      }
    }

    for (auto* docker : dockers)
    {
      alignDocker(docker);
    }

    _aligning = false;
  }

  void alignDocker(PlotDocker* docker)
  {
    if (!docker || !docker->isVisible())
    {
      return;
    }

    std::vector<PlotGeometry> infos;
    infos.reserve(static_cast<size_t>(docker->plotCount()));

    for (int index = 0; index < docker->plotCount(); ++index)
    {
      PlotWidget* plot = docker->plotAt(index);
      if (!plot || !plot->isVisible() || !plot->layout())
      {
        continue;
      }

      PlotGeometry info;
      info.plot = plot;
      info.outer = QRect(plot->mapToGlobal(QPoint(0, 0)), plot->size());
      info.canvas = plot->canvasGeometryGlobal();
      info.old_left_margin = plot->property(kAppliedLeftMargin).toInt();
      info.old_right_margin = plot->property(kAppliedRightMargin).toInt();

      // Remove only the margin that this alignment helper applied on the
      // previous pass. This lets us compare the plots' natural rendered canvas
      // positions without disturbing any unrelated layout margin.
      info.natural_canvas_left = info.canvas.left() - info.old_left_margin;
      info.natural_canvas_right = info.canvas.right() + info.old_right_margin;
      infos.push_back(info);
    }

    std::vector<std::vector<size_t>> groups;
    for (size_t index = 0; index < infos.size(); ++index)
    {
      bool inserted = false;
      for (auto& group : groups)
      {
        if (sameVerticalColumn(infos[index], infos[group.front()]))
        {
          group.push_back(index);
          inserted = true;
          break;
        }
      }
      if (!inserted)
      {
        groups.push_back({ index });
      }
    }

    for (const auto& group : groups)
    {
      if (group.size() < 2)
      {
        continue;
      }

      int target_left = std::numeric_limits<int>::lowest();
      int target_right = std::numeric_limits<int>::max();
      for (size_t index : group)
      {
        target_left = std::max(target_left, infos[index].natural_canvas_left);
        target_right = std::min(target_right, infos[index].natural_canvas_right);
      }

      for (size_t index : group)
      {
        auto& info = infos[index];
        const int required_left = std::max(0, target_left - info.natural_canvas_left);
        const int required_right = std::max(0, info.natural_canvas_right - target_right);

        QMargins margins = info.plot->layout()->contentsMargins();
        const int base_left = std::max(0, margins.left() - info.old_left_margin);
        const int base_right = std::max(0, margins.right() - info.old_right_margin);

        if (required_left != info.old_left_margin || required_right != info.old_right_margin)
        {
          info.plot->setProperty(kAppliedLeftMargin, required_left);
          info.plot->setProperty(kAppliedRightMargin, required_right);
          info.plot->layout()->setContentsMargins(base_left + required_left, margins.top(),
                                                  base_right + required_right, margins.bottom());
          info.plot->layout()->activate();
          info.plot->updateGeometry();
        }
      }
    }
  }

  QApplication* _app = nullptr;
  bool _pending = false;
  bool _aligning = false;
};

void installPlotCanvasAlignmentFix()
{
  auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
  if (app)
  {
    new PlotCanvasAlignmentFix(app);
  }
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(installPlotCanvasAlignmentFix)
