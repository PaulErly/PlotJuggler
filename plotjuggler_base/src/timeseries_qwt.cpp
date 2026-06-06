/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "timeseries_qwt.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <QMessageBox>
#include <QPushButton>
#include <QString>

RangeOpt QwtSeriesWrapper::getVisualizationRangeY(Range range_x)
{
  if (range_x.min <= std::numeric_limits<double>::lowest() &&
      range_x.min <= std::numeric_limits<double>::max())
  {
    return _data->rangeY();
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (size_t i = 0; i < size(); i++)
  {
    const double Y = sample(i).y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  return Range{ min_y, max_y };
}

QString QwtSeriesWrapper::formatValue(double value, int precision) const
{
  return QString::number(value, 'f', precision);
}

RangeOpt QwtTimeseries::getVisualizationRangeY(Range range_X)
{
  int first_index = _ts_data->getIndexFromX(range_X.min + _time_offset);
  int last_index = _ts_data->getIndexFromX(range_X.max + _time_offset);

  if (first_index > last_index || first_index < 0 || last_index < 0)
  {
    return {};
  }

  if (first_index == 0 && last_index == plotData()->size() - 1)
  {
    return _ts_data->rangeY();
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (size_t i = first_index; i < last_index; i++)
  {
    const double Y = sample(i).y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  if (min_y == max_y)
  {
    if (std::abs(min_y) < std::numeric_limits<double>::epsilon())
    {
      min_y = 0.0;
      max_y = 1.0;
    }
    else
    {
      const double pad = std::abs(min_y) * 0.1;
      min_y -= pad;
      max_y += pad;
    }
  }
  return Range{ min_y, max_y };
}

std::optional<QPointF> QwtTimeseries::sampleFromTime(double t)
{
  int index = _ts_data->getIndexFromX(t);
  if (index < 0)
  {
    return {};
  }
  const auto& p = plotData()->at(size_t(index));
  return QPointF(p.x, p.y);
}

TransformedTimeseries::TransformedTimeseries(const PlotData* source_data)
  : QwtTimeseries(&_dst_data), _dst_data(source_data->plotName(), {}), _src_data(source_data)
{
}

TransformFunction::Ptr TransformedTimeseries::transform()
{
  return _transform;
}

bool TransformedTimeseries::setTransform(QString transform_ID)
{
  if (transformName() == transform_ID)
  {
    return true;
  }
  if (transform_ID.isEmpty())
  {
    _transform.reset();
    return false;
  }

  _transform = TransformFactory::create(transform_ID.toStdString());
  if (!_transform)
  {
    return false;
  }
  std::vector<PlotData*> dest = { &_dst_data };
  _dst_data.clear();
  _transform->setData(nullptr, { _src_data }, dest);
  return true;
}

void TransformedTimeseries::updateCache(bool reset_old_data)
{
  if (_transform)
  {
    if (reset_old_data)
    {
      _dst_data.clear();
      _transform->reset();
    }
    std::vector<PlotData*> dest = { &_dst_data };
    _transform->calculate();
  }
  else
  {
    // TODO: optimize ??
    _dst_data.clear();
    for (size_t i = 0; i < _src_data->size(); i++)
    {
      _dst_data.pushBack(_src_data->at(i));
    }
  }
}

QString TransformedTimeseries::transformName()
{
  return (!_transform) ? QString() : _transform->name();
}

QString TransformedTimeseries::alias() const
{
  return _alias;
}

void TransformedTimeseries::setAlias(QString alias)
{
  _alias = alias;
}

QRectF QwtSeriesWrapper::boundingRect() const
{
  if (size() == 0)
  {
    return {};
  }
  auto range_x = plotData()->rangeX().value();
  auto range_y = plotData()->rangeY().value();

  QRectF box;
  box.setLeft(range_x.min);
  box.setRight(range_x.max);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QRectF QwtTimeseries::boundingRect() const
{
  if (size() == 0)
  {
    return {};
  }
  auto range_x = plotData()->rangeX().value();
  auto range_y = plotData()->rangeY().value();

  QRectF box;
  box.setLeft(range_x.min - _time_offset);
  box.setRight(range_x.max - _time_offset);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QPointF QwtSeriesWrapper::sample(size_t i) const
{
  const auto& p = _data->at(i);
  return QPointF(p.x, p.y);
}

QPointF QwtTimeseries::sample(size_t i) const
{
  const auto& p = _ts_data->at(i);
  return QPointF(p.x - _time_offset, p.y);
}

size_t QwtSeriesWrapper::size() const
{
  return _data->size();
}

void QwtTimeseries::setTimeOffset(double offset)
{
  _time_offset = offset;
}

RangeOpt QwtSeriesWrapper::getVisualizationRangeX()
{
  if (this->size() < 2)
  {
    return {};
  }
  else
  {
    return _data->rangeX();
  }
}

RangeOpt QwtTimeseries::getVisualizationRangeX()
{
  if (this->size() < 2)
  {
    return {};
  }
  else
  {
    auto range = _ts_data->rangeX().value();
    return RangeOpt({ range.min - _time_offset, range.max - _time_offset });
  }
}

const PlotDataBase<double, double>* QwtSeriesWrapper::plotData() const
{
  return _data;
}

QPointF QwtStringTimeseries::sample(size_t i) const
{
  const auto& p = _data->at(i);
  return QPointF(p.x - _time_offset, p.y.index);
}

size_t QwtStringTimeseries::size() const
{
  return _data->size();
}

QRectF QwtStringTimeseries::boundingRect() const
{
  if (size() == 0)
  {
    return {};
  }
  auto range_x = _data->rangeX().value();

  QRectF box;
  box.setLeft(range_x.min - _time_offset);
  box.setRight(range_x.max - _time_offset);
  box.setTop(double(_data->stringCount() - 1));
  box.setBottom(0.0);
  return box;
}

void QwtStringTimeseries::setTimeOffset(double offset)
{
  _time_offset = offset;
}

RangeOpt QwtStringTimeseries::getVisualizationRangeX()
{
  if (size() < 2)
  {
    return {};
  }
  auto range = _data->rangeX().value();
  return Range{ range.min - _time_offset, range.max - _time_offset };
}

RangeOpt QwtStringTimeseries::getVisualizationRangeY(Range range_x)
{
  int first_index = _data->getIndexFromX(range_x.min + _time_offset);
  int last_index = _data->getIndexFromX(range_x.max + _time_offset);
  if (first_index > last_index || first_index < 0 || last_index < 0)
  {
    return {};
  }

  double min_y = std::numeric_limits<double>::max();
  double max_y = std::numeric_limits<double>::lowest();
  for (int i = first_index; i <= last_index; i++)
  {
    const double y = _data->at(size_t(i)).y.index;
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  }

  if (min_y == max_y)
  {
    const double pad = (std::abs(min_y) < 1.0) ? 1.0 : std::abs(min_y) * 0.1;
    min_y -= pad;
    max_y += pad;
  }
  return Range{ min_y, max_y };
}

QString QwtStringTimeseries::formatValue(double value, int) const
{
  const auto index = static_cast<uint32_t>(std::llround(value));
  const auto text = _data->getString(StringDictIndex(index));
  return QString::fromUtf8(text.data(), int(text.size())).toHtmlEscaped();
}
