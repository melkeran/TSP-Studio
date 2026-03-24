#include "ui/TSPStudioMapView.h"
#include "ui/GuiController.h"
#include "core/TSPStudioManager.h"
#include "core/Tracer.h"

#include <QSGNode>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <QCursor>
#include <QQuickWindow>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Pre-compute unit circle vertices once
static void getCircleOffsets(float* outX, float* outY, int segments) {
    for (int i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(2.0 * M_PI * i / segments);
        outX[i] = std::cos(angle);
        outY[i] = std::sin(angle);
    }
}

TSPStudioMapView::TSPStudioMapView(QQuickItem* parent) : QQuickItem(parent) {
    TRACE_SCOPE;
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
}

QObject* TSPStudioMapView::controller() const {
    return m_controller;
}

void TSPStudioMapView::setController(QObject* controller) {
    TRACE_SCOPE;
    auto* gc = qobject_cast<GuiController*>(controller);
    if (m_controller == gc) return;

    if (m_controller) m_controller->disconnect(this);

    m_controller = gc;
    if (m_controller) {
        m_manager = m_controller->getManager();
        connect(m_controller, &GuiController::viewResetNeeded, this, &TSPStudioMapView::resetView);
        connect(m_controller, &GuiController::dataChanged,     this, &TSPStudioMapView::scheduleRepaint);
        connect(m_controller, &GuiController::routeChanged,    this, &TSPStudioMapView::scheduleRepaint);
        connect(m_controller, &GuiController::showDelaunayChanged, this, &TSPStudioMapView::scheduleRepaint);
        connect(m_controller, &GuiController::cityAdded, this, [this](double x, double y){
            m_effects.push_back({QPointF(x,y), 0.0f, false}); update();
        });
        connect(m_controller, &GuiController::cityRemoved, this, [this](double x, double y){
            m_citiesDirty = true; m_delaunayDirty = true; m_routeDirty = true;
            m_effects.push_back({QPointF(x,y), 0.0f, true}); update();
        });
    } else {
        m_manager = nullptr;
    }

    emit controllerChanged();
    update();
}

void TSPStudioMapView::setCityRadius(double r) {
    m_cityRadius = std::clamp(r, 0.0, 50.0);
    m_citiesDirty = true;
    emit cityRadiusChanged();
    update();
}

void TSPStudioMapView::setCitySize(double size) {
    m_cityRadius = std::clamp(size, 0.0, 50.0);
    m_citiesDirty = true;
    emit cityRadiusChanged();
    polish();
    update();
}

void TSPStudioMapView::setCityColor(const QColor& c) {
    if (m_cityColor == c) return;
    m_cityColor = c;
    m_citiesDirty = true;
    emit cityColorChanged();
    update();
}

void TSPStudioMapView::setRouteColor(const QColor& c) {
    if (m_routeColor != c) { m_routeColor = c; emit routeColorChanged(); scheduleRepaint(); }
}

void TSPStudioMapView::setDelaunayColor(const QColor& c) {
    if (m_delaunayColor != c) { m_delaunayColor = c; emit delaunayColorChanged(); scheduleRepaint(); }
}

void TSPStudioMapView::setRouteThickness(double t) {
    if (m_routeThickness != t) { m_routeThickness = t; emit routeThicknessChanged(); scheduleRepaint(); }
}

void TSPStudioMapView::resetView() {
    m_hoveredCityIndex = -1;
    m_draggedCityIndex = -1;
    computeFitView();
}

void TSPStudioMapView::scheduleRepaint() {
    m_citiesDirty = true;
    m_routeDirty = true;
    m_delaunayDirty = true;
    update();
}

void TSPStudioMapView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size() && oldGeometry.width() > 0 && oldGeometry.height() > 0) {
        // Maintain centering of the current view during resize
        m_offsetX += (newGeometry.width() - oldGeometry.width()) / 2.0;
        m_offsetY += (newGeometry.height() - oldGeometry.height()) / 2.0;
        update();
    }
}

// ============================================================
// Coordinate Transforms
// ============================================================
QPointF TSPStudioMapView::screenToWorld(const QPointF& sp) const {
    double s = (std::abs(m_scale) > 1e-12) ? m_scale : 1.0;
    return QPointF((sp.x() - m_offsetX) / s + m_viewCenterX, (sp.y() - m_offsetY) / s + m_viewCenterY);
}

QPointF TSPStudioMapView::worldToScreen(double wx, double wy) const {
    // Relative to the stabilizing origin to preserve IEEE 754 precision
    double dx = wx - m_viewCenterX;
    double dy = wy - m_viewCenterY;
    
    // Centers the screen at (m_offsetX, m_offsetY) if m_viewCenterX/Y were (minX, minY)
    // But since they are the actual center, we adjust slightly
    double sx = dx * m_scale + m_offsetX;
    double sy = dy * m_scale + m_offsetY;
    
    // Safety: Extreme zoom/pan can produce coordinates that crash graphics drivers
    if (!std::isfinite(sx) || std::abs(sx) > 1e8) sx = -10000;
    if (!std::isfinite(sy) || std::abs(sy) > 1e8) sy = -10000;
    
    return QPointF(sx, sy);
}

int TSPStudioMapView::findCityAtScreen(const QPointF& screenPos) const {
    if (!m_manager) return -1;
    const auto* tree = m_manager->getKDTree();
    
    // Fallback to O(N) if tree not ready yet, but preferred O(log N)
    if (tree) {
        QPointF wp = screenToWorld(screenPos);
        auto res = tree->nearest(CDT::V2d<double>(wp.x(), wp.y()), m_manager->getCities());
        if (res.second != static_cast<CDT::VertInd>(-1)) {
            QPointF sp = worldToScreen(res.first.x, res.first.y);
            double dx = sp.x() - screenPos.x();
            double dy = sp.y() - screenPos.y();
            double hitRadius = std::max(m_cityRadius + 10.0, 20.0);
            if (dx*dx + dy*dy < hitRadius*hitRadius) return static_cast<int>(res.second);
        }
        return -1;
    }

    const auto& cities = m_manager->getCities();
    double hitRadius = std::max(m_cityRadius + 10.0, 20.0);
    double hitRadiusSq = hitRadius * hitRadius;
    int bestIdx = -1;
    double bestDistSq = hitRadiusSq;

    for (size_t i = 0; i < cities.size(); ++i) {
        QPointF sp = worldToScreen(cities[i].x, cities[i].y);
        double dx = sp.x() - screenPos.x();
        double dy = sp.y() - screenPos.y();
        double dSq = dx * dx + dy * dy;
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

void TSPStudioMapView::computeFitView() {
    TRACE_SCOPE;
    if (!m_manager || m_manager->getCities().empty()) {
        m_scale = 1.0;
        m_offsetX = 0.0;
        m_offsetY = 0.0;
        m_viewCenterX = 0.0;
        m_viewCenterY = 0.0;
        update();
        return;
    }
    const auto& cities = m_manager->getCities();
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto& c : cities) {
        if (c.x < minX) minX = c.x; if (c.x > maxX) maxX = c.x;
        if (c.y < minY) minY = c.y; if (c.y > maxY) maxY = c.y;
    }
    double pad = 50.0;
    double w = width() > 1 ? width() : 800;
    double h = height() > 1 ? height() : 600;
    double spreadX = std::max(maxX - minX, 1.0);
    double spreadY = std::max(maxY - minY, 1.0);
    m_viewCenterX = minX + spreadX / 2.0;
    m_viewCenterY = minY + spreadY / 2.0;
    m_scale = std::min((w - 2.0 * pad) / spreadX, (h - 2.0 * pad) / spreadY);
    m_offsetX = w / 2.0;
    m_offsetY = h / 2.0;
    m_citiesDirty = true;
    m_routeDirty = true;
    m_delaunayDirty = true;
    update();
}

// ============================================================
// Scene Graph Rendering
// ============================================================

static QSGGeometryNode* getOrCreatePolyNode(QSGNode* parent, int index, QColor color, int drawMode, float lineWidth = 1.0f) {
    int i = 0;
    QSGNode* child = parent->firstChild();
    while (child && i < index) {
        child = child->nextSibling();
        i++;
    }
    QSGGeometryNode* node = nullptr;
    if (!child) {
        node = new QSGGeometryNode;
        auto* geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geom->setDrawingMode(drawMode);
        geom->setLineWidth(lineWidth);
        geom->setVertexDataPattern(QSGGeometry::DynamicPattern);
        auto* mat = new QSGFlatColorMaterial;
        mat->setColor(color);
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        parent->appendChildNode(node);
    } else {
        node = static_cast<QSGGeometryNode*>(child);
        auto* mat = static_cast<QSGFlatColorMaterial*>(node->material());
        if (mat && mat->color() != color) {
            mat->setColor(color);
            node->markDirty(QSGNode::DirtyMaterial);
        }
        if (node->geometry()->drawingMode() != drawMode) {
            node->geometry()->setDrawingMode(drawMode);
            node->markDirty(QSGNode::DirtyGeometry);
        }
        if (node->geometry()->lineWidth() != lineWidth) {
            node->geometry()->setLineWidth(lineWidth);
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }
    return node;
}

static void hideExtraChildren(QSGNode* parent, int keepCount) {
    int i = 0;
    QSGNode* child = parent->firstChild();
    while (child) {
        if (i >= keepCount) {
            if (auto* node = static_cast<QSGGeometryNode*>(child)) {
                if (node->geometry()->vertexCount() != 0) {
                    node->geometry()->allocate(0);
                    node->markDirty(QSGNode::DirtyGeometry);
                }
            }
        }
        child = child->nextSibling();
        i++;
    }
}

QSGNode* TSPStudioMapView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QSGTransformNode* root = (oldNode && oldNode->childCount() == 5) 
                                ? static_cast<QSGTransformNode*>(oldNode) : nullptr;

    if (!root) {
        delete oldNode;
        root = new QSGTransformNode;
        for (int i = 0; i < 5; ++i) root->appendChildNode(new QSGNode);
    }
    const auto& citiesData = m_manager ? m_manager->getCities() : std::vector<City>();
    const int ncTotal = static_cast<int>(citiesData.size());
    const bool hasCities = (ncTotal > 0);
    const int MAX_VERTS_PER_CHUNK = 16380; 
    const float vCenterX = static_cast<float>(m_viewCenterX);
    const float vCenterY = static_cast<float>(m_viewCenterY);

    // Check if we need to rebuild geometry (only if data changed or scale drifted > 30%)
    double scaleDrift = (m_rebuiltScale > 0) ? std::abs(m_scale / m_rebuiltScale - 1.0) : 10.0;
    bool mustRebuild = m_citiesDirty || m_routeDirty || m_delaunayDirty || (scaleDrift > 0.3);
    if (mustRebuild) {
        m_rebuiltScale = m_scale; 
        m_citiesDirty = m_routeDirty = m_delaunayDirty = true; 
    }

    // Apply GPU-side translation and smooth zooming
    QMatrix4x4 matrix;
    matrix.translate(static_cast<float>(m_offsetX), static_cast<float>(m_offsetY));
    if (std::abs(m_rebuiltScale) > 1e-12) {
        float relScale = static_cast<float>(m_scale / m_rebuiltScale);
        matrix.scale(relScale);
    }
    root->setMatrix(matrix);
    root->markDirty(QSGNode::DirtyMatrix);

    // Safety check children count
    while (root->childCount() < 5) root->appendChildNode(new QSGNode);
    QSGNode* delaunayParent   = root->childAtIndex(0);
    QSGNode* routeParent      = root->childAtIndex(1);
    QSGNode* citiesParent     = root->childAtIndex(2);
    QSGNode* highlightsParent = root->childAtIndex(3);
    QSGNode* effectsParent    = root->childAtIndex(4);

    // Helper to transform world to local pixels relative to view center
    auto worldToLocalPixels = [&](double wx, double wy) -> QPointF {
        return QPointF((wx - m_viewCenterX) * m_scale, (wy - m_viewCenterY) * m_scale);
    };
    
    // ── Animation / Effects ──
    // Note: Effects are drawn in screen space, so we need to compensate for the root transform
    // Alternatively, move effectsParent outside root. For simplicity, we just un-apply transform here.
    QMatrix4x4 invMatrix = matrix.inverted();

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    float fdt = (m_lastUpdateMs == 0) ? 0.0f : (now - m_lastUpdateMs) / 1000.0f;
    m_lastUpdateMs = now;

    if (!m_effects.empty()) {
        for (auto it = m_effects.begin(); it != m_effects.end(); ) {
            it->progress += fdt * 2.0f; 
            if (it->progress >= 1.0f) it = m_effects.erase(it);
            else ++it;
        }
        update(); 
    } else {
        m_lastUpdateMs = 0;
    }

    int currentFxCount = 0;
    for (const auto& fx : m_effects) {
        QColor c = fx.removing ? QColor("#ef4444") : QColor("#10b981");
        c.setAlphaF((1.0f - fx.progress) * 0.8f);
        QSGGeometryNode* fxNode = getOrCreatePolyNode(effectsParent, currentFxCount++, c, QSGGeometry::DrawTriangleStrip);
        
        const float rScale = static_cast<float>(m_rebuiltScale);
        QPointF lp((fx.worldPos.x() - m_viewCenterX) * rScale, (fx.worldPos.y() - m_viewCenterY) * rScale);
        float cx = static_cast<float>(lp.x()), cy = static_cast<float>(lp.y());
        float r1 = (static_cast<float>(m_cityRadius)) * (1.0f + fx.progress * 4.0f);
        float r2 = r1 + 3.0f;
        int segs = 20;
        fxNode->geometry()->allocate((segs + 1) * 2);
        auto* v = fxNode->geometry()->vertexDataAsPoint2D();
        for (int s = 0; s <= segs; ++s) {
            float theta = static_cast<float>(2.0 * M_PI * s / segs);
            v[s*2].set(cx + r1*std::cos(theta), cy + r1*std::sin(theta));
            v[s*2+1].set(cx + r2*std::cos(theta), cy + r2*std::sin(theta));
        }
        fxNode->markDirty(QSGNode::DirtyGeometry);
    }
    hideExtraChildren(effectsParent, currentFxCount);

    // ── Delaunay Triangulation ──
    const auto& delEdges = m_manager ? m_manager->getDelaunayEdges() : std::vector<uint32_t>();
    bool showDelaunay = hasCities && !delEdges.empty() && m_controller && m_controller->showDelaunay();
    if (showDelaunay) {
        if (m_delaunayDirty || delEdges.size() != m_lastDelaunayCount) {
            m_delaunayDirty = false;
            m_lastDelaunayCount = delEdges.size();
            const float rScale = static_cast<float>(m_rebuiltScale);
            QColor dColor = m_delaunayColor; dColor.setAlphaF(0.40f);
            int vertsNeeded = static_cast<int>(delEdges.size());
            int numChunks = (vertsNeeded + MAX_VERTS_PER_CHUNK - 1) / MAX_VERTS_PER_CHUNK;

            for (int c = 0; c < numChunks; ++c) {
                QSGGeometryNode* node = getOrCreatePolyNode(delaunayParent, c, dColor, QSGGeometry::DrawLines);
                int startIdx = c * MAX_VERTS_PER_CHUNK;
                int countIdx = std::min(MAX_VERTS_PER_CHUNK, vertsNeeded - startIdx);
                node->geometry()->allocate(countIdx);
                auto* v = node->geometry()->vertexDataAsPoint2D();
                for (int i = 0; i < countIdx; i += 2) {
                    uint32_t a = delEdges[startIdx + i];
                    uint32_t b = delEdges[startIdx + i + 1];
                    if (a < (uint32_t)ncTotal && b < (uint32_t)ncTotal) {
                        float x1 = (static_cast<float>(citiesData[a].x) - vCenterX) * rScale;
                        float y1 = (static_cast<float>(citiesData[a].y) - vCenterY) * rScale;
                        float x2 = (static_cast<float>(citiesData[b].x) - vCenterX) * rScale;
                        float y2 = (static_cast<float>(citiesData[b].y) - vCenterY) * rScale;
                        v[i].set(x1, y1);
                        v[i+1].set(x2, y2);
                    }
                }
                node->markDirty(QSGNode::DirtyGeometry);
            }
            hideExtraChildren(delaunayParent, numChunks);
        }
    } else {
        hideExtraChildren(delaunayParent, 0);
        m_lastDelaunayCount = 0;
    }

    // ── Route Lines ──
    const std::vector<int> route = m_controller ? m_controller->getRouteSafe() : std::vector<int>();
    bool isSolving = m_controller && m_controller->solving();

    if (hasCities && !route.empty()) {
        if (m_routeDirty || route != m_lastRoute) {
            m_routeDirty = false;
            m_lastRoute = route;
            const float rScale = static_cast<float>(m_rebuiltScale);
            int rn = static_cast<int>(route.size());
            if (routeParent->childCount() < 2) {
                while(routeParent->childCount() > 0) {
                    auto* child = routeParent->childAtIndex(0);
                    routeParent->removeChildNode(child);
                    delete child;
                }
                routeParent->appendChildNode(new QSGNode); 
                routeParent->appendChildNode(new QSGNode); 
            }
            QSGNode* oldBatch = routeParent->childAtIndex(0);
            QSGNode* newBatch = routeParent->childAtIndex(1);

            std::vector<int> oldEdges;
            std::vector<int> newEdges;
            
            bool useHighlights = isSolving && !m_lastRenderedRoute.empty() && 
                                 m_lastRenderedRoute.size() == (size_t)rn;

            if (useHighlights) {
                struct Adj { int a, b; };
                std::vector<Adj> prevAdj(rn, {-1, -1});
                for (size_t i = 0; i < m_lastRenderedRoute.size(); ++i) {
                    int u = m_lastRenderedRoute[i];
                    int v = m_lastRenderedRoute[(i + 1) % m_lastRenderedRoute.size()];
                    if (u >= 0 && u < rn) { (prevAdj[u].a == -1) ? prevAdj[u].a = v : prevAdj[u].b = v; }
                    if (v >= 0 && v < rn) { (prevAdj[v].a == -1) ? prevAdj[v].a = u : prevAdj[v].b = u; }
                }
                for (int i = 0; i < rn; ++i) {
                    int u = route[i]; int v = route[(i + 1) % rn];
                    if (u >= 0 && u < rn && (prevAdj[u].a == v || prevAdj[u].b == v)) {
                        oldEdges.push_back(u); oldEdges.push_back(v);
                    } else {
                        newEdges.push_back(u); newEdges.push_back(v);
                    }
                }
            } else {
                for (int i = 0; i < rn; ++i) {
                    oldEdges.push_back(route[i]); oldEdges.push_back(route[(i + 1) % rn]);
                }
            }
            m_lastRenderedRoute = route;

            auto drawEdges = [&](QSGNode* parent, const std::vector<int>& edges, QColor color, float lineWidth) {
                int totalSegments = static_cast<int>(edges.size()) / 2;
                int vpc = (MAX_VERTS_PER_CHUNK / 6) * 6;
                int segmentsPerChunk = vpc / 6;
                int numChunks = (totalSegments + segmentsPerChunk - 1) / segmentsPerChunk;
                float hw = lineWidth * 0.5f;

                for (int c = 0; c < numChunks; ++c) {
                    QSGGeometryNode* node = getOrCreatePolyNode(parent, c, color, QSGGeometry::DrawTriangles, 1.0f);
                    int startSeg = c * segmentsPerChunk;
                    int countThisChunk = std::min(segmentsPerChunk, totalSegments - startSeg);
                    node->geometry()->allocate(countThisChunk * 6);
                    auto* v = node->geometry()->vertexDataAsPoint2D();
                 for (int s = 0; s < countThisChunk; ++s) {
                    int idx1 = edges[(startSeg + s) * 2];
                    int idx2 = edges[(startSeg + s) * 2 + 1];
                    int vIdx = s * 6;
                    
                    if (idx1 >= 0 && idx1 < ncTotal && idx2 >= 0 && idx2 < ncTotal) {
                        float x1 = (static_cast<float>(citiesData[idx1].x) - vCenterX) * rScale;
                        float y1 = (static_cast<float>(citiesData[idx1].y) - vCenterY) * rScale;
                        float x2 = (static_cast<float>(citiesData[idx2].x) - vCenterX) * rScale;
                        float y2 = (static_cast<float>(citiesData[idx2].y) - vCenterY) * rScale;
                        float dx = x2 - x1; float dy = y2 - y1;
                        float len = std::sqrt(dx*dx + dy*dy);
                        if (len > 0.0001f) { dx /= len; dy /= len; }
                        float nx = -dy * hw; float ny = dx * hw;
                        v[vIdx].set(x1 + nx, y1 + ny); v[vIdx+1].set(x2 + nx, y2 + ny); v[vIdx+2].set(x1 - nx, y1 - ny);
                        v[vIdx+3].set(x2 + nx, y2 + ny); v[vIdx+4].set(x2 - nx, y2 - ny); v[vIdx+5].set(x1 - nx, y1 - ny);
                    } else {
                        for(int j=0; j<6; ++j) v[vIdx+j].set(0,0);
                    }
                }
                    node->markDirty(QSGNode::DirtyGeometry);
                }
                hideExtraChildren(parent, numChunks);
            };

            float baseThickness = static_cast<float>(m_routeThickness);
            drawEdges(oldBatch, oldEdges, m_routeColor, baseThickness);
            drawEdges(newBatch, newEdges, QColor("#facc15"), baseThickness * 2.5f);
        }
    } else {
        if (routeParent->childCount() >= 2) {
            hideExtraChildren(routeParent->childAtIndex(0), 0);
            hideExtraChildren(routeParent->childAtIndex(1), 0);
        }
        m_lastRoute.clear();
    }

    // ── Cities ──
    if (hasCities && m_cityRadius > 1e-6) {
        const int cpc = MAX_VERTS_PER_CHUNK / VERTS_PER_CITY;
        const int numChunks = (ncTotal + cpc - 1) / cpc;

        if (ncTotal != static_cast<int>(m_lastCityCount)) {
            m_citiesDirty = true;
            m_lastCityCount = ncTotal;
        }

        if (m_citiesDirty) {
            m_citiesDirty = false;
            const float rScale = static_cast<float>(m_rebuiltScale);
            const float r = static_cast<float>(m_cityRadius);
            
            for (int c = 0; c < numChunks; ++c) {
                QSGGeometryNode* node = getOrCreatePolyNode(citiesParent, c, m_cityColor, QSGGeometry::DrawTriangles);
                const int startCity = c * cpc;
                const int count = std::min(cpc, ncTotal - startCity);
                node->geometry()->allocate(count * VERTS_PER_CITY);
                auto* vRaw = reinterpret_cast<float*>(node->geometry()->vertexData());
                for (int i = 0; i < count; ++i) {
                    const auto& city = citiesData[startCity + i];
                    float cx = (static_cast<float>(city.x) - vCenterX) * rScale;
                    float cy = (static_cast<float>(city.y) - vCenterY) * rScale;
                    int base = i * 12;
                    vRaw[base] = cx - r;   vRaw[base+1] = cy - r;
                    vRaw[base+2] = cx + r; vRaw[base+3] = cy - r;
                    vRaw[base+4] = cx - r; vRaw[base+5] = cy + r;
                    vRaw[base+6] = cx + r; vRaw[base+7] = cy - r;
                    vRaw[base+8] = cx + r; vRaw[base+9] = cy + r;
                    vRaw[base+10] = cx - r; vRaw[base+11] = cy + r;
                }
                node->markDirty(QSGNode::DirtyGeometry);
            }
            hideExtraChildren(citiesParent, numChunks);
        }
    } else {
        hideExtraChildren(citiesParent, 0);
        m_lastCityCount = 0;
    }

    // ── Highlights ──
    int hi = (m_draggedCityIndex != -1) ? m_draggedCityIndex : m_hoveredCityIndex;
    if (hi >= 0 && hi < ncTotal && m_cityRadius > 1e-6) {
        QSGGeometryNode* hNode = getOrCreatePolyNode(highlightsParent, 0, QColor("#f59e0b"), QSGGeometry::DrawTriangleStrip);
        const float rScale = static_cast<float>(m_rebuiltScale);
        float cx = (static_cast<float>(citiesData[hi].x) - vCenterX) * rScale;
        float cy = (static_cast<float>(citiesData[hi].y) - vCenterY) * rScale;
        float r1 = static_cast<float>(m_cityRadius) + 4.0f;
        float r2 = r1 + 6.0f;
        hNode->geometry()->allocate(10);
        auto* v = hNode->geometry()->vertexDataAsPoint2D();
        v[0].set(cx - r1, cy - r1); v[1].set(cx - r2, cy - r2);
        v[2].set(cx + r1, cy - r1); v[3].set(cx + r2, cy - r2);
        v[4].set(cx + r1, cy + r1); v[5].set(cx + r2, cy + r2);
        v[6].set(cx - r1, cy + r1); v[7].set(cx - r2, cy + r2);
        v[8].set(cx - r1, cy - r1); v[9].set(cx - r2, cy - r2);
        hNode->markDirty(QSGNode::DirtyGeometry);
        hideExtraChildren(highlightsParent, 1);
    } else {
        hideExtraChildren(highlightsParent, 0);
    }

    return root;
}

// ============================================================
// Mouse Handling
// ============================================================
void TSPStudioMapView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    m_pressPos = event->position();
    m_didDrag = false;

    if (event->button() == Qt::RightButton) {
        if (m_controller && m_controller->solving()) { event->accept(); return; }
        int idx = findCityAtScreen(event->position());
        if (idx >= 0 && m_controller) {
            m_controller->removeCity(idx);
            m_hoveredCityIndex = -1;
            setCursor(Qt::ArrowCursor);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_draggedCityIndex = -1;
        m_isPanning = true;
    }

    if (event->button() == Qt::LeftButton) {
        int cityIdx = findCityAtScreen(event->position());
        if (cityIdx >= 0) {
            if (m_controller && m_controller->solving()) { 
                m_draggedCityIndex = -1;
                m_isPanning = false; // Left click doesn't pan anymore
            } else {
                m_draggedCityIndex = cityIdx;
                m_isPanning = false;
            }
        } else {
            m_draggedCityIndex = -1;
            m_isPanning = false; // Left click on empty area doesn't pan anymore
        }
    }
    update();
    event->accept();
}

void TSPStudioMapView::mouseMoveEvent(QMouseEvent* event) {
    QPointF delta = event->position() - m_lastMousePos;
    double movedDist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    if (movedDist > 3.0) m_didDrag = true;

    if (m_isPanning) {
        setCursor(Qt::ClosedHandCursor);
        m_offsetX += delta.x();
        m_offsetY += delta.y();
        m_lastMousePos = event->position();
        update();
    } else if (m_draggedCityIndex >= 0 && m_controller) {
        setCursor(Qt::ClosedHandCursor);
        QPointF wp = screenToWorld(event->position());
        m_controller->updateCityPosition(m_draggedCityIndex, wp.x(), wp.y());
        m_lastMousePos = event->position();
        update();
    }
    event->accept();
}

void TSPStudioMapView::mouseReleaseEvent(QMouseEvent* event) {
    m_isPanning = false;
    m_draggedCityIndex = -1;
    m_hoveredCityIndex = findCityAtScreen(event->position());
    setCursor(m_hoveredCityIndex >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
    event->accept();
}

void TSPStudioMapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_controller && m_controller->solving()) { event->accept(); return; }
        int idx = findCityAtScreen(event->position());
        if (idx < 0 && m_controller) {
            QPointF wp = screenToWorld(event->position());
            m_controller->addCity(wp.x(), wp.y());
        }
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        computeFitView();
        update();
    }
    event->accept();
}

void TSPStudioMapView::wheelEvent(QWheelEvent* event) {
    double oldScale = m_scale;
    double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    m_scale *= factor;
    m_scale = std::clamp(m_scale, 1e-12, 1e12);

    QPointF mp = event->position();
    m_offsetX = mp.x() - (mp.x() - m_offsetX) * (m_scale / oldScale);
    m_offsetY = mp.y() - (mp.y() - m_offsetY) * (m_scale / oldScale);

    m_hoveredCityIndex = findCityAtScreen(event->position());
    setCursor(m_hoveredCityIndex >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
    event->accept();
}

void TSPStudioMapView::hoverMoveEvent(QHoverEvent* event) {
    if (m_controller && m_controller->solving()) { 
        if (m_hoveredCityIndex != -1) { m_hoveredCityIndex = -1; update(); }
        setCursor(Qt::ArrowCursor); 
        event->accept();
        return; 
    }
    int idx = findCityAtScreen(event->position());
    if (idx != m_hoveredCityIndex) {
        m_hoveredCityIndex = idx;
        setCursor(idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    event->accept();
}

void TSPStudioMapView::hoverLeaveEvent(QHoverEvent* event) {
    m_hoveredCityIndex = -1;
    setCursor(Qt::ArrowCursor);
    update();
    event->accept();
}
