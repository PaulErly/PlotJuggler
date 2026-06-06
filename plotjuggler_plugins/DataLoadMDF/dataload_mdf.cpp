/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "dataload_mdf.h"

#include <mdf/ichannelgroup.h>
#include <mdf/ichannelconversion.h>
#include <mdf/ichannelobserver.h>
#include <mdf/idatagroup.h>
#include <mdf/mdffile.h>
#include <mdf/mdfreader.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace PJ;

namespace
{

std::string makeSafeName(const std::string& name, const std::string& fallback)
{
  return name.empty() ? fallback : name;
}

std::string makeSeriesName(size_t data_group_index, size_t channel_group_index,
                           const mdf::IChannelObserver& observer)
{
  (void)data_group_index;
  (void)channel_group_index;
  return makeSafeName(observer.Channel().DisplayName(),
                      makeSafeName(observer.Name(), "channel"));
}

bool isTextLikeConversion(const mdf::IChannelObserver& observer)
{
  const auto* conversion = observer.Channel().ChannelConversion();
  if (!conversion)
  {
    return false;
  }

  const auto type = conversion->Type();
  return type == mdf::ConversionType::ValueToText ||
         type == mdf::ConversionType::ValueRangeToText ||
         type == mdf::ConversionType::TextToTranslation;
}

bool readTime(const mdf::IChannelObserver& master, uint64_t sample, double& time)
{
  if (master.GetEngValue(sample, time) && std::isfinite(time))
  {
    return true;
  }

  return master.GetChannelValue(sample, time) && std::isfinite(time);
}

std::optional<double> firstTimeOffset(const mdf::IChannelObserver& master)
{
  const auto samples = master.NofSamples();
  for (uint64_t sample = 0; sample < samples; sample++)
  {
    double time = 0.0;
    if (readTime(master, sample, time))
    {
      return time;
    }
  }
  return std::nullopt;
}

bool hasTextValue(const mdf::IChannelObserver& observer, uint64_t sample,
                  std::string& text_value)
{
  if (observer.GetEngValue(sample, text_value) && !text_value.empty())
  {
    return true;
  }

  text_value = observer.EngValueToString(sample);
  return !text_value.empty();
}

bool importNumericSeries(const mdf::IChannelObserver& master,
                         const mdf::IChannelObserver& observer,
                         const std::string& series_name, PlotGroup::Ptr group,
                         PlotDataMapRef& plot_data, double time_offset)
{
  auto series = plot_data.addNumeric(series_name, group);
  const auto samples = std::min(master.NofSamples(), observer.NofSamples());

  for (uint64_t sample = 0; sample < samples; sample++)
  {
    double time = 0.0;
    double value = 0.0;

    if (readTime(master, sample, time) && observer.GetEngValue(sample, value) &&
        std::isfinite(value))
    {
      series->second.pushBack({ time - time_offset, value });
    }
  }

  if (series->second.size() == 0)
  {
    plot_data.erase(series_name);
    return false;
  }

  return true;
}

bool importStringSeries(const mdf::IChannelObserver& master,
                        const mdf::IChannelObserver& observer,
                        const std::string& series_name, PlotGroup::Ptr group,
                        PlotDataMapRef& plot_data, double time_offset)
{
  auto series = plot_data.addStringSeries(series_name, group);
  const auto samples = std::min(master.NofSamples(), observer.NofSamples());

  for (uint64_t sample = 0; sample < samples; sample++)
  {
    double time = 0.0;
    std::string value;

    if (readTime(master, sample, time) && hasTextValue(observer, sample, value))
    {
      series->second.pushBack({ time - time_offset, value });
    }
  }

  if (series->second.size() == 0)
  {
    plot_data.erase(series_name);
    return false;
  }

  return true;
}

std::string uniqueSeriesName(const std::string& base_name, const PlotDataMapRef& plot_data,
                             size_t data_group_index, size_t channel_group_index)
{
  if (plot_data.numeric.count(base_name) == 0 && plot_data.strings.count(base_name) == 0 &&
      plot_data.user_defined.count(base_name) == 0)
  {
    return base_name;
  }

  std::ostringstream out;
  out << base_name << " [DG" << data_group_index << "/CG" << channel_group_index << "]";
  return out.str();
}

bool importObserver(const mdf::IChannelObserver& master,
                    const mdf::IChannelObserver& observer,
                    const std::string& series_name, PlotGroup::Ptr group,
                    PlotDataMapRef& plot_data, double time_offset)
{
  if (observer.IsMaster() || observer.IsArray())
  {
    return false;
  }

  if (isTextLikeConversion(observer))
  {
    return importStringSeries(master, observer, series_name, group, plot_data, time_offset);
  }

  if (importNumericSeries(master, observer, series_name, group, plot_data, time_offset))
  {
    return true;
  }

  return importStringSeries(master, observer, series_name, group, plot_data, time_offset);
}

}  // namespace

const std::vector<const char*>& DataLoadMDF::compatibleFileExtensions() const
{
  static std::vector<const char*> extensions = { "mf4", "mdf", "m4f" };
  return extensions;
}

bool DataLoadMDF::readDataFromFile(FileLoadInfo* fileload_info, PlotDataMapRef& plot_data)
{
  const std::string filename = QFileInfo(fileload_info->filename).absoluteFilePath().toStdString();

  if (!mdf::IsMdfFile(filename))
  {
    throw std::runtime_error("MDF: file is not a valid MDF/MF4 file");
  }

  mdf::MdfReader reader(filename);
  if (!reader.IsOk())
  {
    throw std::runtime_error("MDF: failed to open file");
  }

  if (!reader.ReadEverythingButData())
  {
    throw std::runtime_error("MDF: failed to read file metadata");
  }

  const auto* file = reader.GetFile();
  if (!file)
  {
    throw std::runtime_error("MDF: failed to read file structure");
  }

  mdf::DataGroupList data_groups;
  file->DataGroups(data_groups);

  size_t imported_series = 0;
  size_t skipped_groups = 0;

  for (size_t data_group_index = 0; data_group_index < data_groups.size(); data_group_index++)
  {
    auto* data_group = data_groups[data_group_index];
    if (!data_group)
    {
      continue;
    }

    const auto channel_groups = data_group->ChannelGroups();
    for (size_t channel_group_index = 0; channel_group_index < channel_groups.size();
         channel_group_index++)
    {
      const auto* channel_group = channel_groups[channel_group_index];
      if (!channel_group || channel_group->NofSamples() == 0)
      {
        skipped_groups++;
        continue;
      }

      mdf::ChannelObserverList observers;
      mdf::CreateChannelObserverForChannelGroup(*data_group, *channel_group, observers);

      auto master_it = std::find_if(observers.begin(), observers.end(), [](const auto& observer) {
        return observer && observer->IsMaster();
      });

      if (master_it == observers.end())
      {
        skipped_groups++;
        continue;
      }

      if (!reader.ReadData(*data_group))
      {
        skipped_groups++;
        continue;
      }

      const auto time_offset = firstTimeOffset(**master_it).value_or(0.0);

      for (const auto& observer : observers)
      {
        if (!observer)
        {
          continue;
        }

        const auto base_name = makeSeriesName(data_group_index, channel_group_index, *observer);
        const auto series_name = uniqueSeriesName(base_name, plot_data, data_group_index,
                                                  channel_group_index);
        if (importObserver(**master_it, *observer, series_name, {}, plot_data, time_offset))
        {
          imported_series++;
        }
      }

      data_group->ClearData();
    }
  }

  if (imported_series == 0)
  {
    std::ostringstream message;
    message << "MDF: no plottable scalar channels were imported";
    if (skipped_groups > 0)
    {
      message << " (" << skipped_groups << " channel groups skipped)";
    }
    throw std::runtime_error(message.str());
  }

  return true;
}

bool DataLoadMDF::xmlSaveState(QDomDocument&, QDomElement&) const
{
  return true;
}

bool DataLoadMDF::xmlLoadState(const QDomElement&)
{
  return true;
}
