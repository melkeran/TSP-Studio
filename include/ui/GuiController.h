#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QThread>
#include <QVariantMap>
#include <QColor>
#include <QMutex>
#include <QPointer>
#include <vector>
#include "core/TSPStudioManager.h"

class SolverWorker;

class GuiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool tracingEnabled READ tracingEnabled WRITE setTracingEnabled NOTIFY tracingEnabledChanged)
    Q_PROPERTY(int cityCount READ cityCount NOTIFY dataChanged)
    Q_PROPERTY(bool solving READ solving NOTIFY solvingStatusChanged)
    Q_PROPERTY(bool showDelaunay READ showDelaunay WRITE setShowDelaunay NOTIFY showDelaunayChanged)

    // Persistent Settings
    Q_PROPERTY(double cityPointSize READ cityPointSize WRITE setCityPointSize NOTIFY cityPointSizeChanged)
    Q_PROPERTY(double routeLineThickness READ routeLineThickness WRITE setRouteLineThickness NOTIFY routeLineThicknessChanged)
    Q_PROPERTY(QColor cityColor READ cityColor WRITE setCityColor NOTIFY cityColorChanged)
    Q_PROPERTY(QColor routeColor READ routeColor WRITE setRouteColor NOTIFY routeColorChanged)
    Q_PROPERTY(QColor delaunayColor READ delaunayColor WRITE setDelaunayColor NOTIFY delaunayColorChanged)
    Q_PROPERTY(QString lastAlgorithm READ lastAlgorithm WRITE setLastAlgorithm NOTIFY lastAlgorithmChanged)
public:
    explicit GuiController(QObject* parent = nullptr);
    ~GuiController() override;

    Q_INVOKABLE void loadTSPFile(const QString& fileUrl);
    Q_INVOKABLE void runSolver(const QString& algorithm);
    Q_INVOKABLE void plotInitialSolution();
    Q_INVOKABLE void stopSolver();
    Q_INVOKABLE void addCity(double x, double y);
    Q_INVOKABLE void removeCity(int index);
    Q_INVOKABLE void updateCityPosition(int index, double newX, double newY);
    Q_INVOKABLE void clearCities();
    Q_INVOKABLE void rotateCities(double angleDegrees);
    Q_INVOKABLE void flipCities(bool horizontal);
    Q_INVOKABLE void setAlgorithmParam(const QString& key, QVariant value);
    Q_INVOKABLE void resetTSPStudio();
    Q_INVOKABLE void saveRoute(const QString& fileUrl);
    Q_INVOKABLE void loadRoute(const QString& fileUrl);

    // Settings Management
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void loadSettings();
    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE QVariant getParam(const QString& key, const QVariant& defaultValue = QVariant()) const { return m_params.value(key, defaultValue); }

    double cityPointSize() const { return m_cityPointSize; }
    void setCityPointSize(double s) { if (m_cityPointSize != s) { m_cityPointSize = s; emit cityPointSizeChanged(); } }
    double routeLineThickness() const { return m_routeLineThickness; }
    void setRouteLineThickness(double t) { if (m_routeLineThickness != t) { m_routeLineThickness = t; emit routeLineThicknessChanged(); } }
    QColor cityColor() const { return m_cityColor; }
    void setCityColor(const QColor& c) { if (m_cityColor != c) { m_cityColor = c; emit cityColorChanged(); } }
    QColor routeColor() const { return m_routeColor; }
    void setRouteColor(const QColor& c) { if (m_routeColor != c) { m_routeColor = c; emit routeColorChanged(); } }
    QColor delaunayColor() const { return m_delaunayColor; }
    void setDelaunayColor(const QColor& c) { if (m_delaunayColor != c) { m_delaunayColor = c; emit delaunayColorChanged(); } }
    QString lastAlgorithm() const { return m_lastAlgorithm; }
    void setLastAlgorithm(const QString& a) { if (m_lastAlgorithm != a) { m_lastAlgorithm = a; emit lastAlgorithmChanged(); } }
    int cityCount() const { return static_cast<int>(m_tspManager.getCities().size()); }
    bool solving() const { return m_solverThread != nullptr && m_solverThread->isRunning(); }
    bool showDelaunay() const { return m_showDelaunay; }
    void setShowDelaunay(bool show) { if (m_showDelaunay != show) { m_showDelaunay = show; emit showDelaunayChanged(); } }

    Q_INVOKABLE void computeDelaunay(bool silent = false);
    bool tracingEnabled() const;
    void setTracingEnabled(bool e);

    TSPStudioManager* getManager() { return &m_tspManager; }
    std::vector<int> getRouteSafe() const { 
        QMutexLocker locker(&m_routeMutex);
        return m_route; 
    }
    std::vector<int> getPreviousRouteSafe() const {
        QMutexLocker locker(&m_routeMutex);
        return m_previousRoute;
    }

signals:
    void tracingEnabledChanged();
    // viewResetNeeded = recalculate scale/offset to fit all cities (file load, clear, etc.)
    void viewResetNeeded();
    // dataChanged = just redraw, don't change the view (add/remove/move a single city)
    void dataChanged();
    // routeChanged = route line data updated
    void routeChanged();

    void solvingStatusChanged(bool isSolving);
    void newLogMessage(const QString& msg);
    void metricsUpdated(double distance, int timeMs);
    void cityAdded(double x, double y);
    void cityRemoved(double x, double y);
    void showDelaunayChanged();

    void cityPointSizeChanged();
    void routeLineThicknessChanged();
    void cityColorChanged();
    void routeColorChanged();
    void delaunayColorChanged();
    void lastAlgorithmChanged();
    void paramsChanged();

private slots:
    void onRouteUpdated(const std::vector<int>& route, double distance);
    void onSolverFinished(double finalDistance, int elapsedMs);

private:
    TSPStudioManager m_tspManager;
    std::vector<int> m_route;
    std::vector<int> m_previousRoute;
    mutable QMutex m_routeMutex;
    QVariantMap m_params;

    QPointer<QThread> m_solverThread;
    QPointer<SolverWorker> m_solverWorker;
    bool m_showDelaunay = false;

    // Default values
    double m_cityPointSize = 0.0;
    double m_routeLineThickness = 1.0;
    QColor m_cityColor = QColor("#000000");
    QColor m_routeColor = QColor("#38bdf8");
    QColor m_delaunayColor = QColor("#8b5cf6");
    QString m_lastAlgorithm = "Iterated Local Search (ILS)";

    // Transformation persistence
    double m_savedRotation = 0.0;
    bool m_savedFlipH = false;
    bool m_savedFlipV = false;

    void cleanupSolver();
};
