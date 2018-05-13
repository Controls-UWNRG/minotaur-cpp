#include "astar.h"

#include "../gui/griddisplay.h"
#include "../compstate/parammanager.h"

#include <set>
#include <map>

#ifndef NDEBUG

#include <cassert>

#endif

#define TERRAIN_WALL -1

typedef std::pair<double, vector2i> associated_cost;

static double manhattan_dist(
    const vector2i &cur,
    const vector2i &dest
) {
    return abs(dest.x() - cur.x()) + abs(dest.y() - cur.y());
}

static bool is_valid(
    int x, int y,
    int max_row, int max_col
) {
    return (x >= 0) && (x < max_row) && (y >= 0) && (y < max_col);
}

static void get_neighbors(
    const vector2i &c,
    int max_row, int max_col,
    std::vector<vector2i> &neighbors
) {
    int cur_x = c.x();
    int cur_y = c.y();
    if (is_valid(cur_x - 1, cur_y, max_row, max_col)) {
        neighbors.emplace_back(cur_x - 1, cur_y);
    }
    if (is_valid(cur_x + 1, cur_y, max_row, max_col)) {
        neighbors.emplace_back(cur_x + 1, cur_y);
    }
    if (is_valid(cur_x, cur_y - 1, max_row, max_col)) {
        neighbors.emplace_back(cur_x, cur_y - 1);
    }
    if (is_valid(cur_x, cur_y + 1, max_row, max_col)) {
        neighbors.emplace_back(cur_x, cur_y + 1);
    }
}

static void backtrack(
    const vector2i &start,
    const vector2i &dest,
    const std::map<vector2i, vector2i> &parent,
    std::vector<vector2i> &path
) {
    vector2i cur = dest;
    while (cur != start) {
        path.push_back(cur);
        cur = parent.at(cur);
    }
    path.push_back(start);
    std::reverse(std::begin(path), std::end(path));
}

void nrg::search_path(
    array2d<int> &terrain,
    const vector2i &start,
    const vector2i &dest,
    std::vector<vector2i> &path
) {
    std::map<vector2i, vector2i> parent;
    std::map<vector2i, double> cost;
    std::set<associated_cost> open_set;

    open_set.emplace(0, start);
    parent[start] = start;
    cost[start] = 0;

    int max_row = static_cast<int>(terrain.x());
    int max_col = static_cast<int>(terrain.y());

    while (!open_set.empty()) {
        vector2i cur = open_set.begin()->second;
        open_set.erase(open_set.begin());
        if (cur == dest) { break; }

        std::vector<vector2i> neighbors;
        get_neighbors(cur, max_row, max_col, neighbors);
        for (const vector2i &next : neighbors) {
            if (terrain[next.x()][next.y()] != TERRAIN_WALL) {
                double new_cost = cost[cur] + terrain[next.x()][next.y()];
                if (!cost.count(next) || new_cost < cost[next]) {
                    cost[next] = new_cost;
                    open_set.emplace(new_cost + manhattan_dist(next, dest), next);
                    parent[next] = cur;
                }
            }
        }
    }
    backtrack(start, dest, parent, path);
}

static void apply_kernel(
    array2d<int> &source,
    array2d<int> &target,
    unsigned int x, unsigned int y,
    int wall, int wp0, int wp1, int wp2
) {
    using std::max;
    using std::min;
    using std::abs;
#ifndef NDEBUG
    assert(x < source.x());
    assert(y < source.y());
    assert(source[x][y] == wall);
#endif
    constexpr unsigned int offset = 3;
    int wp[] = {0, wp0, wp1, wp2};
    int x_min = -min(offset, x);
    int y_min = -min(offset, y);
    int x_max = min(offset, static_cast<int>(source.x()) - x - 1);
    int y_max = min(offset, static_cast<int>(source.y()) - y - 1);
    for (int dx = x_min; dx <= x_max; ++dx) {
        for (int dy = y_min; dy <= y_max; ++dy) {
            int tx = x + dx;
            int ty = y + dy;
            if (source[tx][ty] != wall) {
                target[tx][ty] += wp[max(abs(dx), abs(dy))];
            }
        }
    }
}

static void kernelize(
    array2d<int> &source,
    array2d<int> &target,
    int wall, int wp0, int wp1, int wp2
) {
#ifndef NDEBUG
    assert(source.x() == target.x());
    assert(source.y() == target.y());
#endif
    for (unsigned int x = 0; x < source.x(); ++x) {
        for (unsigned int y = 0; y < source.y(); ++y) {
            if (source[x][y] == wall) {
                target[x][y] = TERRAIN_WALL;
                //apply_kernel(
                //    source, target,
                //    x, y, wall,
                //    wp0, wp1, wp2
                //);
            }
        }
    }
}

array2d<int> nrg::grid_kernelize(
    weak_ref<GridDisplay> grid,
    weak_ref<param_manager> pm
) {
    constexpr int wall = GridDisplay::DEFAULT_WEIGHT;
    int wp0 = pm->wall_penalty_0;
    int wp1 = pm->wall_penalty_1;
    int wp2 = pm->wall_penalty_2;
    auto mx = static_cast<std::size_t>(grid->get_num_cols());
    auto my = static_cast<std::size_t>(grid->get_num_rows());
    array2d<int> source(mx, my);
    array2d<int> terrain(mx, my);
    array2d<int> &selected = grid->selected();
    for (unsigned int x = 0; x < mx; ++x) {
        memcpy(source[x].get(), selected[x].get(), my * sizeof(int));
    }
    kernelize(source, terrain, wall, wp0, wp1, wp2);
    return terrain; // move constructor
}

std::vector<vector2i> nrg::grid_path(
    weak_ref<GridDisplay> grid,
    weak_ref<param_manager> pm
) {
    std::vector<vector2i> path;
    array2d<int> terrain = nrg::grid_kernelize(grid, pm);
    const vector2i &start = grid->get_pos_start();
    const vector2i &dest = grid->get_pos_end();
    search_path(terrain, start, dest, path);
    return path; // move constructor
}

void nrg::scale_path_pixels(
    weak_ref<GridDisplay> grid,
    std::vector<vector2i> &path
) {
    constexpr int b = GridDisplay::GRID_SIZE;
    constexpr int b2 = b / 2;
    int gx = grid->x();
    int gy = grid->y();
    for (vector2i &v : path) {
        v.x() = gx + b2 + v.x() * b;
        v.y() = gy + b2 + v.y() * b;
    }
}

void nrg::connect_path(
    weak_ref<GridDisplay> grid,
    weak_ref<param_manager> pm
) {
#ifndef NDEBUG
    assert(dynamic_cast<ImageViewer *>(grid->parent()) != nullptr);
#endif
    std::vector<vector2i> path = nrg::grid_path(grid, pm);
    nrg::scale_path_pixels(grid, path);
    weak_ref<ImageViewer> viewer = dynamic_cast<ImageViewer *>(grid->parent());
    viewer->set_path(path);
}
