#include <gtest/gtest.h>

#include <QApplication>

#include "PlotJuggler/plotdata.h"
#include "qwt_plot.h"
#include "qwt_plot_curve.h"
#include "qwt_plot_layout.h"
#include "qwt_scale_draw.h"
#include "timeseries_qwt.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{

class QtWidgetTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!QApplication::instance())
    {
      if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
      {
        qputenv("QT_QPA_PLATFORM", "offscreen");
      }
      app_ = std::make_unique<QApplication>(argc_, argv_);
    }
  }

private:
  inline static int argc_ = 1;
  inline static char app_name_[] = "plotjuggler_tests";
  inline static char* argv_[] = { app_name_, nullptr };
  inline static std::unique_ptr<QApplication> app_;
};

class LongCategoricalScaleDraw : public QwtScaleDraw
{
public:
  QwtText label(double value) const override
  {
    switch (static_cast<int>(std::llround(value)))
    {
      case 0:
        return "Standby";
      case 1:
        return "Ready2Charge";
      case 2:
        return "Charging";
      case 3:
        return "StopChargingBecauseTheStateNameIsLong";
      default:
        return {};
    }
  }
};

void activateLayout(QwtPlot& plot)
{
  plot.updateAxes();
  plot.plotLayout()->activate(&plot, plot.contentsRect());
}

double axisExtent(const QwtPlot& plot, QwtAxisId axis_id)
{
  const auto* axis = plot.axisWidget(axis_id);
  return axis->scaleDraw()->extent(axis->font());
}

}  // namespace

TEST_F(QtWidgetTest, SharedScaleDrawExtentAlignsCanvasForNumericAndCategoricalPlots)
{
  QwtPlot numeric;
  QwtPlot categorical;
  numeric.resize(640, 220);
  categorical.resize(640, 220);

  for (auto* plot : { &numeric, &categorical })
  {
    plot->plotLayout()->setAlignCanvasToScales(true);
    plot->setAxisScale(QwtPlot::xBottom, 0.0, 100.0);
    plot->setAxisScale(QwtPlot::yLeft, 0.0, 3.0);
  }
  categorical.setAxisScaleDraw(QwtPlot::yLeft, new LongCategoricalScaleDraw);

  activateLayout(numeric);
  activateLayout(categorical);
  const auto numeric_before = numeric.plotLayout()->canvasRect();
  const auto categorical_before = categorical.plotLayout()->canvasRect();
  ASSERT_GT(categorical_before.left(), numeric_before.left());

  const double max_left_extent =
      std::max(axisExtent(numeric, QwtPlot::yLeft), axisExtent(categorical, QwtPlot::yLeft));
  numeric.axisWidget(QwtPlot::yLeft)->scaleDraw()->setMinimumExtent(max_left_extent);
  categorical.axisWidget(QwtPlot::yLeft)->scaleDraw()->setMinimumExtent(max_left_extent);

  activateLayout(numeric);
  activateLayout(categorical);
  const auto numeric_after = numeric.plotLayout()->canvasRect();
  const auto categorical_after = categorical.plotLayout()->canvasRect();

  EXPECT_NEAR(numeric_after.left(), categorical_after.left(), 1.0);
  EXPECT_NEAR(numeric_after.right(), categorical_after.right(), 1.0);

  const double numeric_x = numeric.canvasMap(QwtPlot::xBottom).transform(78.593);
  const double categorical_x = categorical.canvasMap(QwtPlot::xBottom).transform(78.593);
  EXPECT_NEAR(numeric_x, categorical_x, 1.0);
}

TEST(TimeSeriesStorage, DuplicateTimestampsReachQwtSeriesInOrder)
{
  PJ::PlotData numeric("numeric", {});
  numeric.pushBack({ 77.0, 0.0 });
  numeric.pushBack({ 78.593, 0.0 });
  numeric.pushBack({ 78.593, 1.0 });
  numeric.pushBack({ 78.593, 0.0 });
  numeric.pushBack({ 79.0, 2.0 });

  QwtTimeseries numeric_qwt(&numeric);
  ASSERT_EQ(numeric_qwt.size(), 5U);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(1).x(), 78.593);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(1).y(), 0.0);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(2).x(), 78.593);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(2).y(), 1.0);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(3).x(), 78.593);
  EXPECT_DOUBLE_EQ(numeric_qwt.sample(3).y(), 0.0);

  PJ::StringSeries strings("state", {});
  strings.pushBack({ 78.593, "Standby" });
  strings.pushBack({ 78.593, "Ready2Charge" });
  strings.pushBack({ 78.593, "StopCharging" });

  QwtStringTimeseries string_qwt(&strings);
  ASSERT_EQ(string_qwt.size(), 3U);
  EXPECT_DOUBLE_EQ(string_qwt.sample(0).x(), 78.593);
  EXPECT_DOUBLE_EQ(string_qwt.sample(1).x(), 78.593);
  EXPECT_DOUBLE_EQ(string_qwt.sample(2).x(), 78.593);
  EXPECT_EQ(string_qwt.formatValue(string_qwt.sample(0).y(), 3), "Standby");
  EXPECT_EQ(string_qwt.formatValue(string_qwt.sample(1).y(), 3), "Ready2Charge");
  EXPECT_EQ(string_qwt.formatValue(string_qwt.sample(2).y(), 3), "StopCharging");
}

TEST(TimeSeriesStorage, ExplicitCategoricalValuesPreserveEnumCodes)
{
  PJ::StringSeries states("module_state", {});
  states.pushBackMapped(1.0, 2U, "MODULESTS_ON");
  states.pushBackMapped(2.0, 0U, "MODULESTS_OFF");
  states.pushBackMapped(3.0, 2U, "MODULESTS_ON");

  QwtStringTimeseries qwt(&states);
  ASSERT_EQ(qwt.size(), 3U);

  // The first state encountered is ON, but it must remain at its source enum
  // value (2) rather than being compacted to categorical index 0.
  EXPECT_DOUBLE_EQ(qwt.sample(0).y(), 2.0);
  EXPECT_DOUBLE_EQ(qwt.sample(1).y(), 0.0);
  EXPECT_DOUBLE_EQ(qwt.sample(2).y(), 2.0);
  EXPECT_EQ(qwt.formatValue(0.0, 3), "MODULESTS_OFF");
  EXPECT_EQ(qwt.formatValue(2.0, 3), "MODULESTS_ON");
  EXPECT_EQ(states.stringCount(), 3U);
}

TEST(TimeSeriesStorage, OutOfOrderInsertionKeepsDuplicateTimestampBlock)
{
  PJ::PlotData numeric("numeric", {});
  numeric.pushBack({ 80.0, 8.0 });
  numeric.pushBack({ 78.593, 0.0 });
  numeric.pushBack({ 78.593, 1.0 });
  numeric.pushBack({ 78.593, 0.0 });

  ASSERT_EQ(numeric.size(), 4U);
  EXPECT_DOUBLE_EQ(numeric.at(0).x, 78.593);
  EXPECT_DOUBLE_EQ(numeric.at(1).x, 78.593);
  EXPECT_DOUBLE_EQ(numeric.at(2).x, 78.593);
  EXPECT_DOUBLE_EQ(numeric.at(0).y, 0.0);
  EXPECT_DOUBLE_EQ(numeric.at(1).y, 1.0);
  EXPECT_DOUBLE_EQ(numeric.at(2).y, 0.0);
  EXPECT_DOUBLE_EQ(numeric.at(3).x, 80.0);
}

TEST(TimeSeriesVisualization, ConstantZeroFullRangeHasVisibleYAxisSpan)
{
  PJ::PlotData numeric("constant_zero", {});
  numeric.pushBack({ 0.0, 0.0 });
  numeric.pushBack({ 1.0, 0.0 });
  numeric.pushBack({ 2.0, 0.0 });

  QwtTimeseries numeric_qwt(&numeric);
  const auto range = numeric_qwt.getVisualizationRangeY({ 0.0, 2.0 });

  ASSERT_TRUE(range.has_value());
  EXPECT_LT(range->min, range->max);
  EXPECT_DOUBLE_EQ(range->min, 0.0);
  EXPECT_DOUBLE_EQ(range->max, 1.0);
}

TEST(TimeSeriesVisualization, ConstantNonzeroFullRangeIsPaddedAroundValue)
{
  PJ::PlotData numeric("constant_voltage", {});
  numeric.pushBack({ 0.0, 12.0 });
  numeric.pushBack({ 1.0, 12.0 });
  numeric.pushBack({ 2.0, 12.0 });

  QwtTimeseries numeric_qwt(&numeric);
  const auto range = numeric_qwt.getVisualizationRangeY({ 0.0, 2.0 });

  ASSERT_TRUE(range.has_value());
  EXPECT_LT(range->min, 12.0);
  EXPECT_GT(range->max, 12.0);
  EXPECT_LT(range->min, range->max);
}
