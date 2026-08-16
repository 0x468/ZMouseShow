#include "zmouse/platform/spotlight_region.hpp"
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void test_shape(const zmouse::overlay::SpotlightShape shape, const std::string_view name)
{
    const zmouse::overlay::Rect bounds{0, 0, 100, 100};
    const HRGN region = zmouse::platform::create_spotlight_region(bounds, shape);
    check(region != nullptr, name);
    if (region == nullptr)
    {
        return;
    }

    check(PtInRegion(region, 50, 50) != FALSE, "every spotlight shape contains its center");
    check(PtInRegion(region, 0, 0) == FALSE, "every spotlight shape excludes its bounding-box corner");
    static_cast<void>(DeleteObject(region));
}

void test_distinct_shape_profiles()
{
    const zmouse::overlay::Rect bounds{0, 0, 100, 100};
    const HRGN circle = zmouse::platform::create_spotlight_region(bounds, zmouse::overlay::SpotlightShape::circle);
    const HRGN rounded =
        zmouse::platform::create_spotlight_region(bounds, zmouse::overlay::SpotlightShape::rounded_square);
    const HRGN diamond = zmouse::platform::create_spotlight_region(bounds, zmouse::overlay::SpotlightShape::diamond);
    if (circle != nullptr && rounded != nullptr && diamond != nullptr)
    {
        check(PtInRegion(circle, 12, 12) == FALSE, "circle keeps its corners dimmed");
        check(PtInRegion(rounded, 12, 12) != FALSE, "rounded square reveals more corner area than circle");
        check(PtInRegion(diamond, 12, 12) == FALSE, "diamond keeps diagonal corners dimmed");
        check(PtInRegion(diamond, 50, 2) != FALSE, "diamond reaches the center of its top edge");
    }
    if (circle != nullptr)
    {
        static_cast<void>(DeleteObject(circle));
    }
    if (rounded != nullptr)
    {
        static_cast<void>(DeleteObject(rounded));
    }
    if (diamond != nullptr)
    {
        static_cast<void>(DeleteObject(diamond));
    }
}
} // namespace

int main()
{
    test_shape(zmouse::overlay::SpotlightShape::circle, "circle region can be created");
    test_shape(zmouse::overlay::SpotlightShape::rounded_square, "rounded-square region can be created");
    test_shape(zmouse::overlay::SpotlightShape::diamond, "diamond region can be created");
    test_distinct_shape_profiles();
    if (failures == 0)
    {
        std::cout << "All spotlight region tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
