#pragma once
#include <QQuickItem>
#include <QPointF>
#include <vector>

class GuiController;
class TSPStudioManager;

class TSPStudioMapView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(double cityRadius READ cityRadius WRITE setCityRadius NOTIFY cityRadiusChanged)
    Q_PROPERTY(QColor cityColor READ cityColor WRITE setCityColor NOTIFY cityColorChanged)
    Q_PROPERTY(QColor routeColor READ routeColor WRITE setRouteColor NOTIFY routeColorChanged)
    Q_PROPERTY(QColor delaunayColor READ delaunayColor WRITE setDelaunayColor NOTIFY delaunayColorChanged)
    Q_PROPERTY(double routeThickness READ routeThickness WRITE setRouteThickness NOTIFY routeThicknessChanged)

public:
    explicit TSPStudioMapView(QQuickItem* parent = nullptr);

    QObject* controller() const;
    void setController(QObject* controller);

    double cityRadius() const { return m_cityRadius; }
    void setCityRadius(double r);
    Q_INVOKABLE void setCitySize(double size);

    QColor cityColor() const { return m_cityColor; }
    void setCityColor(const QColor& c);

    QColor routeColor() const { return m_routeColor; }
    void setRouteColor(const QColor& c);

    QColor delaunayColor() const { return m_delaunayColor; }
    void setDelaunayColor(const QColor& c);

    double routeThickness() const { return m_routeThickness; }
    void setRouteThickness(double t);

public slots:
    void resetView();
    void scheduleRepaint();

signals:
    void controllerChanged();
    void cityRadiusChanged();
    void cityColorChanged();
    void routeColorChanged();
    void delaunayColorChanged();
    void routeThicknessChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    GuiController* m_controller = nullptr;
    TSPStudioManager* m_manager = nullptr;

    double m_scale = 1.0;
    double m_offsetX = 0.0;
    double m_offsetY = 0.0;
    double m_viewCenterX = 0.0; // Stabilizing origin
    double m_viewCenterY = 0.0;
    double m_cityRadius = 4.0;

    QColor m_cityColor = QColor("#10B981");
    QColor m_routeColor = QColor("#38bdf8");
    QColor m_delaunayColor = QColor("#8b5cf6");
    double m_routeThickness = 1.0;

    int m_draggedCityIndex = -1;
    int m_hoveredCityIndex = -1;
    bool m_isPanning = false;
    bool m_didDrag = false;
    QPointF m_lastMousePos;
    QPointF m_pressPos;

    mutable bool m_citiesDirty = true;
    mutable bool m_routeDirty = true;
    mutable bool m_delaunayDirty = true;
    mutable size_t m_lastCityCount = 0;
    mutable size_t m_lastDelaunayCount = 0;
    mutable std::vector<int> m_lastRoute;
    mutable double m_rebuiltScale = 1.0;

    static constexpr int CIRCLE_SEGMENTS = 24;
    std::vector<int> m_lastRenderedRoute;

    QPointF screenToWorld(const QPointF& screenPos) const;
    QPointF worldToScreen(double wx, double wy) const;
    int findCityAtScreen(const QPointF& screenPos) const;
    void computeFitView();

    struct VisualEffect { QPointF worldPos; float progress; bool removing; };
    std::vector<VisualEffect> m_effects;
    qint64 m_lastUpdateMs = 0;

    struct Bounds { double minX, minY, maxX, maxY; };
    mutable std::vector<Bounds> m_cityChunkBounds;
    static constexpr int VERTS_PER_CITY = 6;
    static constexpr int MAX_VERTS_PER_CHUNK = 16380; 
};
