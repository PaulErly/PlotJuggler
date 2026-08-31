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
#include <QDebug>

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

bool sampleIsValid(const mdf::IChannelObserver& observer, uint64_t sample)
{
  const auto& valid_list = observer.GetValidList();
  return valid_list.empty() || sample >= valid_list.size() || valid_list[size_t(sample)];
}

bool diagnosticsEnabled()
{
  return qEnvironmentVariableIsSet("PLOTJUGGLER_MDF_DIAGNOSTICS");
}

bool shouldLogSeriesDiagnostics(const std::string& series_name)
{
  const QByteArray filter = qgetenv("PLOTJUGGLER_MDF_DIAGNOSTIC_SIGNAL");
  return filter.isEmpty() || series_name.find(filter.constData()) != std::string::npos;
}

uint64_t importSampleCount(const mdf::IChannelObserver& master,
                           const mdf::IChannelObserver& observer,
                           const std::string& series_name)
{
  const auto master_samples = master.NofSamples();
  const auto observer_samples = observer.NofSamples();
  if (diagnosticsEnabled() && shouldLogSeriesDiagnostics(series_name) &&
      master_samples != observer_samples)
  {
    qDebug() << "MDF sample-count mismatch for" << QString::fromStdString(series_name)
             << "master samples" << master_samples << "channel samples" << observer_samples;
  }
  return observer_samples;
}

struct ImportStats
{
  uint64_t imported = 0;
  uint64_t invalid = 0;
  uint64_t missing_time = 0;
  uint64_t missing_value = 0;
  uint64_t non_finite = 0;
};

void logImportStats(const std::string& series_name, const ImportStats& stats)
{
  if (!diagnosticsEnabled() || !shouldLogSeriesDiagnostics(series_name))
  {
    return;
  }

  qDebug() << "MDF import stats for" << QString::fromStdString(series_name) << "imported"
           << stats.imported << "invalid" << stats.invalid << "missing_time"
           << stats.missing_time << "missing_value" << stats.missing_value << "non_finite"
           << stats.non_finite;
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
                         PlotDataMapRef& plot_data)
{
  auto series = plot_data.addNumeric(series_name, group);
  const auto samples = importSampleCount(master, observer, series_name);
  ImportStats stats;

  for (uint64_t sample = 0; sample < samples; sample++)
  {
    double time = 0.0;
    double value = 0.0;

    if (!sampleIsValid(observer, sample))
    {
      stats.invalid++;
      continue;
    }
    if (!readTime(master, sample, time))
    {
      stats.missing_time++;
      continue;
    }
    if (!observer.GetEngValue(sample, value))
    {
      stats.missing_value++;
      continue;
    }
    if (!std::isfinite(value))
    {
      stats.non_finite++;
      continue;
    }

    if (diagnosticsEnabled() && shouldLogSeriesDiagnostics(series_name) && time >= 75.0 &&
        time <= 82.0)
    {
      double raw_value = 0.0;
      const bool has_raw = observer.GetChannelValue(sample, raw_value);
      qDebug() << "MDF sample" << QString::fromStdString(series_name) << "index" << sample
               << "time" << time << "raw_valid" << has_raw << "raw" << raw_value << "physical"
               << value << "valid" << true;
    }
    // MDF master channels in the same synchronization domain already share a
    // file-relative time base. Preserve that value so different channel groups
    // retain their true relative start offsets.
    series->second.pushBack({ time, value });
    stats.imported++;
  }

  logImportStats(series_name, stats);

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
                        PlotDataMapRef& plot_data)
{
  auto series = plot_data.addStringSeries(series_name, group);
  const auto samples = importSampleCount(master, observer, series_name);
  ImportStats stats;

  for (uint64_t sample = 0; sample < samples; sample++)
  {
    double time = 0.0;
    std::string value;

    if (!sampleIsValid(observer, sample))
    {
      stats.invalid++;
      continue;
    }
    if (!readTime(master, sample, time))
    {
      stats.missing_time++;
      continue;
    }
    if (!hasTextValue(observer, sample, value))
    {
      stats.missing_value++;
      continue;
    }

    if (diagnosticsEnabled() && shouldLogSeriesDiagnostics(series_name) && time >= 75.0 &&
        time <= 82.0)
    {
      qDebug() << "MDF sample" << QString::fromStdString(series_name) << "index" << sample
               << "time" << time << "physical" << QString::fromStdString(value) << "valid"
               << true;
    }
    // Keep string/categorical series on the same MDF time base as numeric data.
    series->second.pushBack({ time, value });
    stats.imported++;
  }

  logImportStats(series_name, stats);

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
                    PlotDataMapRef& plot_data)
{
  if (observer.IsMaster() || observer.IsArray())
  {
    return false;
  }

  if (isTextLikeConversion(observer))
  {
    return importStringSeries(master, observer, series_name, group, plot_data);
  }

  if (importNumericSeries(master, observer, series_name, group, plot_data))
  {
    return true;
  }

  return importStringSeries(master, observer, series_name, group, plot_data);
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

      for (const auto& observer : observers)
      {
        if (!observer)
        {
          continue;
        }

        const auto base_name = makeSeriesName(data_group_index, channel_group_index, *observer);
        const auto series_name = uniqueSeriesName(base_name, plot_data, data_group_index,
                                                  channel_group_index);
        if (importObserver(**master_it, *observer, series_name, {}, plot_data))
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
