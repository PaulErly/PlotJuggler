/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef PJ_STRINGSERIES_H
#define PJ_STRINGSERIES_H

#include "PlotJuggler/timeseries.h"
#include "PlotJuggler/string_ref_sso.h"
#include "PlotJuggler/string_dict_index.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PJ
{
class StringSeries : public TimeseriesBase<StringDictIndex>
{
public:
  using TimeseriesBase<StringDictIndex>::_points;

  StringSeries(const std::string& name, PlotGroup::Ptr group)
    : TimeseriesBase<StringDictIndex>(name, group)
  {
  }

  StringSeries(const StringSeries& other) = delete;
  StringSeries(StringSeries&& other) = default;

  StringSeries& operator=(const StringSeries& other) = delete;
  StringSeries& operator=(StringSeries&& other) = default;

  virtual void clear() override
  {
    _index_to_string.clear();
    _string_to_index.clear();
    _mapped_index_to_string.clear();
    _mapped_extent = 0;
    TimeseriesBase<StringDictIndex>::clear();
  }

  void pushBack(const Point& p) override
  {
    // Point.y is StringDictIndex — forward to base
    TimeseriesBase<StringDictIndex>::pushBack(p);
  }

  void pushBack(Point&& p) override
  {
    TimeseriesBase<StringDictIndex>::pushBack(std::move(p));
  }

  // Backward-compatible overload: accepts {timestamp, StringRef}
  void pushBack(std::pair<double, StringRef> p)
  {
    const auto& str = p.second;
    if (str.data() == nullptr || str.size() == 0)
    {
      return;
    }
    StringDictIndex idx = internString(std::string_view(str.data(), str.size()));
    TimeseriesBase<StringDictIndex>::pushBack({ p.first, idx });
  }

  // Preserve an explicit non-negative integer categorical value instead of
  // assigning a dense dictionary index. This is used by MDF value-to-text
  // conversions so an enum such as 2 = MODULESTS_ON is plotted at Y=2 even
  // when MODULESTS_ON is the first or only state present in the recording.
  void pushBackMapped(double timestamp, uint32_t categorical_value, std::string_view str)
  {
    if (str.empty() || categorical_value == StringDictIndex::INVALID)
    {
      return;
    }

    _mapped_index_to_string.emplace(categorical_value, std::string(str));
    _mapped_extent = std::max(_mapped_extent, size_t(categorical_value) + 1U);
    TimeseriesBase<StringDictIndex>::pushBack({ timestamp, StringDictIndex(categorical_value) });
  }

  std::string_view getString(StringDictIndex idx) const
  {
    if (!idx.isValid())
    {
      return {};
    }

    const auto mapped_it = _mapped_index_to_string.find(idx.index);
    if (mapped_it != _mapped_index_to_string.end())
    {
      return mapped_it->second;
    }

    if (idx.index >= _index_to_string.size())
    {
      return {};
    }
    return _index_to_string[idx.index];
  }

  size_t stringCount() const
  {
    // QwtStringTimeseries historically uses stringCount() to establish its
    // categorical bounding range. For explicitly mapped values return the
    // numeric extent, not merely the number of labels encountered, so a lone
    // enum value of 2 still has a range that includes Y=2.
    return std::max(_index_to_string.size(), _mapped_extent);
  }

  std::optional<std::string_view> getStringFromX(double x) const
  {
    int index = getIndexFromX(x);
    if (index < 0)
    {
      return std::nullopt;
    }
    return getString(_points[index].y);
  }

  void clonePoints(StringSeries&& other)
  {
    _index_to_string = std::move(other._index_to_string);
    _string_to_index = std::move(other._string_to_index);
    _mapped_index_to_string = std::move(other._mapped_index_to_string);
    _mapped_extent = other._mapped_extent;
    PlotDataBase<double, StringDictIndex>::clonePoints(std::move(other));
  }

  void clonePoints(const StringSeries& other)
  {
    _index_to_string = other._index_to_string;
    _string_to_index = other._string_to_index;
    _mapped_index_to_string = other._mapped_index_to_string;
    _mapped_extent = other._mapped_extent;
    PlotDataBase<double, StringDictIndex>::clonePoints(other);
  }

  void swapData(StringSeries& other)
  {
    TimeseriesBase<StringDictIndex>::swapData(other);
    std::swap(_index_to_string, other._index_to_string);
    std::swap(_string_to_index, other._string_to_index);
    std::swap(_mapped_index_to_string, other._mapped_index_to_string);
    std::swap(_mapped_extent, other._mapped_extent);
  }

private:
  StringDictIndex internString(std::string_view str)
  {
    _tmp_str.assign(str.data(), str.size());
    auto it = _string_to_index.find(_tmp_str);
    if (it != _string_to_index.end())
    {
      return StringDictIndex(it->second);
    }
    uint32_t new_index = static_cast<uint32_t>(_index_to_string.size());
    _index_to_string.push_back(_tmp_str);
    _string_to_index.emplace(_tmp_str, new_index);
    return StringDictIndex(new_index);
  }

  std::string _tmp_str;
  std::vector<std::string> _index_to_string;
  std::unordered_map<std::string, uint32_t> _string_to_index;
  std::unordered_map<uint32_t, std::string> _mapped_index_to_string;
  size_t _mapped_extent = 0;
};

}  // namespace PJ

#endif
