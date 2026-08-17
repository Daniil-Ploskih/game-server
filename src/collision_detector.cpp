#include "collision_detector.h"

#include <algorithm>
#include <cassert>

namespace collision_detector {

CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
    if (b.x == a.x && b.y == a.y){
        throw std::invalid_argument("Points a and b must be different");
    }
    const double u_x = c.x - a.x;
    const double u_y = c.y - a.y;
    const double v_x = b.x - a.x;
    const double v_y = b.y - a.y;
    const double u_dot_v = u_x * v_x + u_y * v_y;
    const double u_len2 = u_x * u_x + u_y * u_y;
    const double v_len2 = v_x * v_x + v_y * v_y;
    const double proj_ratio = u_dot_v / v_len2;
    const double sq_distance = u_len2 - (u_dot_v * u_dot_v) / v_len2;

    return CollectionResult(sq_distance, proj_ratio);
}

std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> events;

    const size_t items_count = provider.ItemsCount();
    const size_t gatherers_count = provider.GatherersCount();

    for (size_t g_idx = 0; g_idx < gatherers_count; ++g_idx) {
        const Gatherer& gatherer = provider.GetGatherer(g_idx);

        if (gatherer.start_pos.x == gatherer.end_pos.x &&
            gatherer.start_pos.y == gatherer.end_pos.y) {
            continue;
        }

        const double gatherer_radius = gatherer.width ;

        for (size_t i_idx = 0; i_idx < items_count; ++i_idx) {
            const Item& item = provider.GetItem(i_idx);

            const double item_radius = item.width ;
            const double collect_radius = gatherer_radius + item_radius;

            CollectionResult result = TryCollectPoint(
                gatherer.start_pos,
                gatherer.end_pos,
                item.position
            );

            if (result.IsCollected(collect_radius)) {
                GatheringEvent event;
                event.item_id = i_idx;
                event.gatherer_id = g_idx;
                event.sq_distance = result.sq_distance;
                event.time = result.proj_ratio;

                events.push_back(event);
            }
        }
    }

    std::sort(events.begin(), events.end(),
              [](const GatheringEvent& a, const GatheringEvent& b) {
                  return a.time < b.time;
              });

    return events;
}

}  // namespace collision_detector