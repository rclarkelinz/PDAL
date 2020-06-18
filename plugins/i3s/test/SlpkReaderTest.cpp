#include <pdal/pdal_test_main.hpp>

#include <nlohmann/json.hpp>

#include "Support.hpp"

#include <pdal/PipelineManager.hpp>
#include <pdal/StageFactory.hpp>
#include <pdal/PointView.hpp>
#include <pdal/util/FileUtils.hpp>
#include <filters/StreamCallbackFilter.hpp>

#include <io/LasReader.hpp>
#include <io/LasWriter.hpp>

#include "../io/Obb.hpp"

using namespace pdal;

TEST(SlpkReaderTest, obb)
{
    using namespace i3s;

    std::string baseString = R"(
        {
            "center" : [ 0, 0, 0 ],
            "halfSize" : [ 2, 1, 1.5 ],
            "quaternion" : [ 0, 0, 0, 1 ]
        }
    )";
    std::string clipString = R"(
        {
            "center" : [ 2, 1, 1 ],
            "halfSize" : [
                2.12132034355,
                0.707106781186,
                1
            ],
            "quaternion" : [
                0,
                0,
                -0.3826834324,
                0.9238795325
            ]
        }
    )";

    NL::json base = NL::json::parse(baseString);
    NL::json clip = NL::json::parse(clipString);

    Obb b(base);
    Obb c(clip);
    bool ok = b.intersect(c);
    std::cerr << "Intersect = " << ok << "!\n";
}

//test small autzen slpk data provided by esri
TEST(SlpkReaderTest, read_local)
{
    StageFactory f;
    //create args
    Options slpk_options;
    slpk_options.add("filename",
        Support::datapath("i3s/SMALL_AUTZEN_LAS_All.slpk"));
    slpk_options.add("threads", 2);
    slpk_options.add("dimensions", "intensity, returns");

    Stage& reader = *f.createStage("readers.slpk");
    reader.setOptions(slpk_options);

    PointTable table;
    reader.prepare(table);

    PointViewSet viewSet = reader.execute(table);
    PointViewPtr view = *viewSet.begin();

    EXPECT_EQ(view->size(), 106u);
    ASSERT_TRUE(table.layout()->hasDim(Dimension::Id::Intensity));
    ASSERT_TRUE(table.layout()->hasDim(Dimension::Id::NumberOfReturns));
    ASSERT_FALSE(table.layout()->hasDim(Dimension::Id::GpsTime));
}


//test small autzen slpk data provided by esri
TEST(SlpkReaderTest, read_stream_local)
{
    StageFactory f;
    //create args
    Options slpk_options;
    slpk_options.add("filename",
        Support::datapath("i3s/SMALL_AUTZEN_LAS_All.slpk"));
    slpk_options.add("threads", 2);
    slpk_options.add("dimensions", "intensity, returns");

    Stage& reader = *f.createStage("readers.slpk");
    reader.setOptions(slpk_options);

    StreamCallbackFilter filt;
    int cnt = 0;
    auto cb = [&cnt](PointRef& p)
    {
        cnt++;
        return true;
    };
    filt.setCallback(cb);
    filt.setInput(reader);

    FixedPointTable table(10);
    filt.prepare(table);
    filt.execute(table);

    EXPECT_EQ(cnt, 106u);
    ASSERT_TRUE(table.layout()->hasDim(Dimension::Id::Intensity));
    ASSERT_TRUE(table.layout()->hasDim(Dimension::Id::NumberOfReturns));
    ASSERT_FALSE(table.layout()->hasDim(Dimension::Id::GpsTime));
}


TEST(SlpkReaderTest, bounded)
{
    StageFactory f;
    //create args
    Options slpk_options;
    BOX3D bounds(-123.077, 44.053, 130, -123.063, 44.06, 175);

    slpk_options.add("filename",
        Support::datapath("i3s/SMALL_AUTZEN_LAS_All.slpk"));
    slpk_options.add("threads", 64);

    Stage& reader = *f.createStage("readers.slpk");
    reader.setOptions(slpk_options);

    PointTable table;
    reader.prepare(table);

    PointViewSet viewSet = reader.execute(table);
    PointViewPtr view = *viewSet.begin();
    EXPECT_EQ(view->size(), 106u);

    Options slpk2_options;
    slpk2_options.add("filename",
        Support::datapath("i3s/SMALL_AUTZEN_LAS_All.slpk"));
    slpk2_options.add("threads", 64);
    slpk2_options.add("bounds", Bounds(bounds));

    Stage& reader2 = *f.createStage("readers.slpk");
    reader2.setOptions(slpk2_options);

    PointTable table2;
    reader2.prepare(table2);
    PointViewSet viewSet2 = reader2.execute(table2);
    PointViewPtr view2 = *viewSet2.begin();
    EXPECT_EQ(view2->size(), 24u);

    double x, y, z;
    uint64_t count = 0;
    // Count the number of points in the full result that
    for (std::size_t i = 0; i < view->size(); i++)
    {
        x = view->getFieldAs<double>(Dimension::Id::X, i);
        y = view->getFieldAs<double>(Dimension::Id::Y, i);
        z = view->getFieldAs<double>(Dimension::Id::Z, i);
        if (bounds.contains(x,y,z))
            count++;
    }

    // Make sure all points in the filtered view are in the bounds
    // we filtered on.
    for (std::size_t i = 0; i < view2->size(); i++)
    {
        x = view2->getFieldAs<double>(Dimension::Id::X, i);
        y = view2->getFieldAs<double>(Dimension::Id::Y, i);
        z = view2->getFieldAs<double>(Dimension::Id::Z, i);
        ASSERT_TRUE(bounds.contains(x,y,z));
    }
    EXPECT_EQ(view2->size(), count);
}
