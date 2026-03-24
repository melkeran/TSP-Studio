/// This Source Code Form is subject to the terms of the Mozilla Public
/// License, v. 2.0. If a copy of the MPL was not distributed with this
/// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef KDTREE_UNIFIED_H
#define KDTREE_UNIFIED_H

#include "CDTUtils.h"
#include <limits>
#include <vector>
#include <queue>
#include <algorithm>

namespace CDT
{
namespace KDTree
{

struct NodeSplitDirection
{
    enum Enum
    {
        X,
        Y,
    };
};

/// Simple tree structure with alternating half splitting nodes
/// @tparam TCoordType type used for storing point coordinate.
/// @tparam NumVerticesInLeaf The number of points per leaf.
/// @tparam InitialStackDepth initial size of stack depth for queries.
template <
    typename TCoordType,
    size_t NumVerticesInLeaf = 32,
    size_t InitialStackDepth = 64,
    size_t StackDepthIncrement = 64>
class KDTree
{
public:
    typedef TCoordType coord_type;
    typedef CDT::V2d<coord_type> point_type;
    typedef CDT::VertInd point_index;
    typedef std::pair<point_type, point_index> value_type;
    typedef std::vector<point_index> point_data_vec;
    typedef point_data_vec::const_iterator pd_cit;
    typedef CDT::VertInd node_index;
    typedef CDT::array<node_index, 2> children_type;

    struct Node
    {
        children_type children;
        point_data_vec data;
        Node() {
            setChildren(node_index(0), node_index(0));
            data.reserve(NumVerticesInLeaf);
        }
        void setChildren(const node_index c1, const node_index c2) {
            children[0] = c1;
            children[1] = c2;
        }
        bool isLeaf() const {
            return children[0] == children[1];
        }
    };

    KDTree()
        : m_root(invalidIndexSizeType)
        , m_rootDir(NodeSplitDirection::X)
        , m_min(point_type(-std::numeric_limits<coord_type>::max(), -std::numeric_limits<coord_type>::max()))
        , m_max(point_type(std::numeric_limits<coord_type>::max(), std::numeric_limits<coord_type>::max()))
        , m_size(0)
        , m_isRootBoxInitialized(false)
    {
        m_root = addNewNode();
    }

    KDTree(const point_type& min, const point_type& max)
        : m_root(invalidIndexSizeType)
        , m_rootDir(NodeSplitDirection::X)
        , m_min(min)
        , m_max(max)
        , m_size(0)
        , m_isRootBoxInitialized(true)
    {
        m_root = addNewNode();
    }

    CDT::VertInd size() const { return m_size; }

    template <typename TPointVec>
    void insert(const point_index& iPoint, const TPointVec& points)
    {
        ++m_size;
        const auto& pos = points[iPoint];
        while(!isInsideBox(pos, m_min, m_max))
        {
            extendTree(pos);
        }
        
        node_index node = m_root;
        point_type min = m_min;
        point_type max = m_max;
        NodeSplitDirection::Enum dir = m_rootDir;

        NodeSplitDirection::Enum newDir(NodeSplitDirection::X);
        coord_type mid(0);
        point_type newMin, newMax;
        while(true)
        {
            if(m_nodes[node].isLeaf())
            {
                point_data_vec& pd = m_nodes[node].data;
                if(pd.size() < NumVerticesInLeaf)
                {
                    pd.push_back(iPoint);
                    return;
                }
                if(!m_isRootBoxInitialized)
                {
                    initializeRootBox(points);
                    min = m_min;
                    max = m_max;
                }
                calcSplitInfo(min, max, dir, mid, newDir, newMin, newMax);
                const node_index c1 = addNewNode(), c2 = addNewNode();
                Node& n = m_nodes[node];
                n.setChildren(c1, c2);
                point_data_vec& c1data = m_nodes[c1].data;
                point_data_vec& c2data = m_nodes[c2].data;
                for(pd_cit it = n.data.begin(); it != n.data.end(); ++it)
                {
                    whichChild(points[*it], mid, dir) == 0 ? c1data.push_back(*it) : c2data.push_back(*it);
                }
                n.data = point_data_vec();
            }
            else
            {
                calcSplitInfo(min, max, dir, mid, newDir, newMin, newMax);
            }
            const std::size_t iChild = whichChild(points[iPoint], mid, dir);
            iChild == 0 ? max = newMax : min = newMin;
            node = m_nodes[node].children[iChild];
            dir = newDir;
        }
    }

    template <typename TPointVec, typename TFilter>
    value_type nearestFiltered(const point_type& searchPos, const TPointVec& points, TFilter filter) const
    {
        value_type out;
        out.second = static_cast<point_index>(-1); // Invalid index
        int iTask = -1;
        coord_type minDistSq = std::numeric_limits<coord_type>::max();
        std::vector<QueryTask> tasksStack(InitialStackDepth);
        tasksStack[++iTask] = QueryTask(m_root, m_min, m_max, m_rootDir, distanceSquaredToBox(searchPos, m_min, m_max));
        
        while(iTask != -1)
        {
            const QueryTask t = tasksStack[iTask--];
            if(t.distSq > minDistSq) continue;
            
            const Node& n = m_nodes[t.node];
            if(n.isLeaf())
            {
                for(pd_cit it = n.data.begin(); it != n.data.end(); ++it)
                {
                    if (!filter(*it)) continue;
                    const auto& p = points[*it];
                    const coord_type dSq = distanceSq(searchPos, p);
                    if(dSq < minDistSq)
                    {
                        minDistSq = dSq;
                        out.first = point_type(p.x, p.y);
                        out.second = *it;
                    }
                }
            }
            else
            {
                coord_type mid(0);
                NodeSplitDirection::Enum newDir(NodeSplitDirection::X);
                point_type newMin = t.min, newMax = t.max;
                coord_type dSqFarther = std::numeric_limits<coord_type>::max();
                calcSplitAndFartherDist(searchPos, t, mid, newDir, newMin, newMax, dSqFarther);

                if(iTask + 2 >= (int)tasksStack.size()) tasksStack.resize(tasksStack.size() + StackDepthIncrement);

                if(isAfterSplit(searchPos, mid, t.dir)) {
                    if(dSqFarther <= minDistSq) tasksStack[++iTask] = QueryTask(n.children[0], t.min, newMax, newDir, dSqFarther);
                    tasksStack[++iTask] = QueryTask(n.children[1], newMin, t.max, newDir, t.distSq);
                } else {
                    if(dSqFarther <= minDistSq) tasksStack[++iTask] = QueryTask(n.children[1], newMin, t.max, newDir, dSqFarther);
                    tasksStack[++iTask] = QueryTask(n.children[0], t.min, newMax, newDir, t.distSq);
                }
            }
        }
        return out;
    }

    template <typename TPointVec>
    value_type nearest(const point_type& searchPos, const TPointVec& points) const
    {
        return nearestFiltered(searchPos, points, [](point_index) { return true; });
    }

    template <typename TPointVec>
    std::vector<point_index> kNearest(const point_type& searchPos, size_t k, const TPointVec& points, int skipIndex = -1) const
    {
        if (k == 0) return {};
        
        struct Neighbor {
            point_index index;
            coord_type distSq;
            bool operator<(const Neighbor& other) const { return distSq < other.distSq; }
        };
        std::priority_queue<Neighbor> pq;
        
        int iTask = -1;
        std::vector<QueryTask> tasksStack(InitialStackDepth);
        tasksStack[++iTask] = QueryTask(m_root, m_min, m_max, m_rootDir, distanceSquaredToBox(searchPos, m_min, m_max));

        while(iTask != -1)
        {
            const QueryTask t = tasksStack[iTask--];
            coord_type threshold = pq.size() < k ? std::numeric_limits<coord_type>::max() : pq.top().distSq;
            if(t.distSq > threshold) continue;

            const Node& n = m_nodes[t.node];
            if(n.isLeaf())
            {
                for(pd_cit it = n.data.begin(); it != n.data.end(); ++it)
                {
                    if ((int)*it == skipIndex) continue;
                    const auto& p = points[*it];
                    const coord_type dSq = distanceSq(searchPos, p);
                    if (pq.size() < k) {
                        pq.push({*it, dSq});
                    } else if (dSq < pq.top().distSq) {
                        pq.pop();
                        pq.push({*it, dSq});
                    }
                }
            }
            else
            {
                coord_type mid(0);
                NodeSplitDirection::Enum newDir(NodeSplitDirection::X);
                point_type newMin = t.min, newMax = t.max;
                coord_type dSqFarther = std::numeric_limits<coord_type>::max();
                calcSplitAndFartherDist(searchPos, t, mid, newDir, newMin, newMax, dSqFarther);

                if(iTask + 2 >= (int)tasksStack.size()) tasksStack.resize(tasksStack.size() + StackDepthIncrement);

                threshold = pq.size() < k ? std::numeric_limits<coord_type>::max() : pq.top().distSq;
                if(isAfterSplit(searchPos, mid, t.dir)) {
                    if(dSqFarther <= threshold) tasksStack[++iTask] = QueryTask(n.children[0], t.min, newMax, newDir, dSqFarther);
                    tasksStack[++iTask] = QueryTask(n.children[1], newMin, t.max, newDir, t.distSq);
                } else {
                    if(dSqFarther <= threshold) tasksStack[++iTask] = QueryTask(n.children[1], newMin, t.max, newDir, dSqFarther);
                    tasksStack[++iTask] = QueryTask(n.children[0], t.min, newMax, newDir, t.distSq);
                }
            }
        }
        
        std::vector<point_index> result;
        while(!pq.empty()) {
            result.push_back(pq.top().index);
            pq.pop();
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

private:
    node_index addNewNode() {
        const node_index newNodeIndex = static_cast<node_index>(m_nodes.size());
        m_nodes.push_back(Node());
        return newNodeIndex;
    }

    static bool isAfterSplit(const point_type& p, coord_type split, NodeSplitDirection::Enum dir) {
        return dir == NodeSplitDirection::X ? p.x > split : p.y > split;
    }

    template <typename TPoint>
    static std::size_t whichChild(const TPoint& p, coord_type split, NodeSplitDirection::Enum dir) {
        return dir == NodeSplitDirection::X ? (p.x > split) : (p.y > split);
    }

    static void calcSplitInfo(const point_type& min, const point_type& max, NodeSplitDirection::Enum dir, coord_type& midOut, NodeSplitDirection::Enum& newDirOut, point_type& newMinOut, point_type& newMaxOut) {
        newMaxOut = max; newMinOut = min;
        if (dir == NodeSplitDirection::X) {
            midOut = (min.x + max.x) / coord_type(2); newDirOut = NodeSplitDirection::Y;
            newMinOut.x = midOut; newMaxOut.x = midOut;
        } else {
            midOut = (min.y + max.y) / coord_type(2); newDirOut = NodeSplitDirection::X;
            newMinOut.y = midOut; newMaxOut.y = midOut;
        }
    }

    template <typename TPoint>
    static bool isInsideBox(const TPoint& p, const point_type& min, const point_type& max) {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }

    template <typename TPoint>
    void extendTree(const TPoint& p) {
        const node_index newRoot = addNewNode();
        const node_index newLeaf = addNewNode();
        if (m_rootDir == NodeSplitDirection::X) {
            m_rootDir = NodeSplitDirection::Y;
            if(p.y < m_min.y) { m_min.y -= m_max.y - m_min.y; m_nodes[newRoot].setChildren(newLeaf, m_root); }
            else { m_max.y += m_max.y - m_min.y; m_nodes[newRoot].setChildren(m_root, newLeaf); }
        } else {
            m_rootDir = NodeSplitDirection::X;
            if(p.x < m_min.x) { m_min.x -= m_max.x - m_min.x; m_nodes[newRoot].setChildren(newLeaf, m_root); }
            else { m_max.x += m_max.x - m_min.x; m_nodes[newRoot].setChildren(m_root, newLeaf); }
        }
        m_root = newRoot;
    }

    template <typename TPointVec>
    void initializeRootBox(const TPointVec& points) {
        const point_data_vec& data = m_nodes[m_root].data;
        m_min = point_type(points[data.front()].x, points[data.front()].y);
        m_max = m_min;
        for(pd_cit it = data.begin(); it != data.end(); ++it) {
            const auto& p = points[*it];
            m_min = point_type(std::min(m_min.x, (coord_type)p.x), std::min(m_min.y, (coord_type)p.y));
            m_max = point_type(std::max(m_max.x, (coord_type)p.x), std::max(m_max.y, (coord_type)p.y));
        }
        const TCoordType padding(1);
        if(m_min.x == m_max.x) { m_min.x -= padding; m_max.x += padding; }
        if(m_min.y == m_max.y) { m_min.y -= padding; m_max.y += padding; }
        m_isRootBoxInitialized = true;
    }

    static coord_type distanceSquaredToBox(const point_type& p, const point_type& min, const point_type& max) {
        const coord_type dx = std::max(std::max(min.x - p.x, coord_type(0)), p.x - max.x);
        const coord_type dy = std::max(std::max(min.y - p.y, coord_type(0)), p.y - max.y);
        return dx * dx + dy * dy;
    }

    template <typename T1, typename T2>
    static coord_type distanceSq(const T1& p1, const T2& p2) {
        coord_type dx = (coord_type)p1.x - (coord_type)p2.x;
        coord_type dy = (coord_type)p1.y - (coord_type)p2.y;
        return dx * dx + dy * dy;
    }
    struct QueryTask {
        node_index node;
        point_type min, max;
        NodeSplitDirection::Enum dir;
        coord_type distSq;
        QueryTask() : node(0), dir(NodeSplitDirection::X), distSq(0) {}
        QueryTask(node_index n, const point_type& mi, const point_type& ma, NodeSplitDirection::Enum d, coord_type ds)
            : node(n), min(mi), max(ma), dir(d), distSq(ds) {}
    };

    void calcSplitAndFartherDist(const point_type& searchPos, const QueryTask& t, coord_type& mid, NodeSplitDirection::Enum& newDir, point_type& newMin, point_type& newMax, coord_type& dSqFarther) const {
        if (t.dir == NodeSplitDirection::X) {
            mid = (t.min.x + t.max.x) / coord_type(2); newDir = NodeSplitDirection::Y;
            newMin.x = mid; newMax.x = mid;
            const coord_type dx = searchPos.x - mid;
            const coord_type dy = std::max(std::max(t.min.y - searchPos.y, coord_type(0)), searchPos.y - t.max.y);
            dSqFarther = dx * dx + dy * dy;
        } else {
            mid = (t.min.y + t.max.y) / coord_type(2); newDir = NodeSplitDirection::X;
            newMin.y = mid; newMax.y = mid;
            const coord_type dx = std::max(std::max(t.min.x - searchPos.x, coord_type(0)), searchPos.x - t.max.x);
            const coord_type dy = searchPos.y - mid;
            dSqFarther = dx * dx + dy * dy;
        }
    }

    node_index m_root;
    std::vector<Node> m_nodes;
    NodeSplitDirection::Enum m_rootDir;
    point_type m_min, m_max;
    CDT::VertInd m_size;
    bool m_isRootBoxInitialized;
};

} // namespace KDTree
} // namespace CDT

#endif
