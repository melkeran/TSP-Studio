#include "ui/GuiController.h"
#include "solvers/SolverWorker.h"
#include <QUrl>
#include <QElapsedTimer>
#include "core/Tracer.h"
#include <QSettings>
#include <cmath>

GuiController::GuiController(QObject* parent) : QObject(parent) { 
    TRACE_SCOPE;
    m_params["initial_tour_method"] = "christofides"; 
    loadSettings();
}

GuiController::~GuiController() { TRACE_SCOPE; cleanupSolver(); }

void GuiController::loadTSPFile(const QString& fileUrl) {
    TRACE_SCOPE;
    QUrl url(fileUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    emit newLogMessage("Parsing: " + path);
    TRACE_MSG("Cleaning up previous solver");
    cleanupSolver();
    m_route.clear();
    m_previousRoute.clear();
    emit routeChanged();
    TRACE_MSG("Calling TSPStudioManager::loadTSPFile");
    if (m_tspManager.loadTSPFile(path)) {
        int n = static_cast<int>(m_tspManager.getCities().size());
        emit newLogMessage("Loaded " + QString::number(n) + " cities.");
        
        // Re-apply saved transformations to the new data
        if (m_savedRotation != 0.0) m_tspManager.rotateCities(m_savedRotation);
        if (m_savedFlipH) m_tspManager.flipCities(true);
        if (m_savedFlipV) m_tspManager.flipCities(false);
        
        emit dataChanged();
        emit viewResetNeeded();

        // Auto-recompute triangulation if the visualization is enabled
        if (m_showDelaunay) {
            computeDelaunay();
        }
    } else {
        TRACE_MSG("Load failed");
        emit newLogMessage("[ERROR] Could not parse file.");
    }
}

void GuiController::runSolver(const QString& algorithm) {
    TRACE_SCOPE;
    if (m_tspManager.getCities().size() < 2) {
        TRACE_MSG("Too few cities to run solver");
        emit newLogMessage("Need >= 2 cities."); return;
    }
    int count = static_cast<int>(m_tspManager.getCities().size());
    TRACE_MSG("Preparing solver environment. Cities: " + QString::number(count));
    if (count < 2) {
        emit newLogMessage("Need at least 2 cities to solve.");
        return;
    }
    cleanupSolver();
    
    // Reset handled by resetTSPStudio(). 
    // Here we continue if m_route is not empty.
    
    emit newLogMessage("Solver: " + algorithm);
    emit solvingStatusChanged(true);
    
    // Auto-compute Delaunay if Christofides is chosen as initial tour method
    if (m_params.value("initial_tour_method").toString() == "christofides") {
        if (m_tspManager.getDelaunayEdges().empty()) {
            computeDelaunay(true); // Silent compute
        }
    }

    TRACE_MSG("Initializing thread and worker");
    m_solverThread = new QThread(this);
    m_solverWorker = new SolverWorker(m_tspManager.getCities());
    m_solverWorker->setParams(m_params);
    m_solverWorker->setDelaunayEdges(m_tspManager.getDelaunayEdges());
    m_solverWorker->setWeightType(m_tspManager.getWeightType());
    m_solverWorker->moveToThread(m_solverThread);

    QString method = "runTwoOpt";
    if (algorithm == "Iterated Local Search (ILS)") method = "runIteratedLocalSearch";
    else if (algorithm == "Guided Local Search (GLS)") method = "runGuidedLocalSearch";
    else if (algorithm == "Genetic Algorithm (GA)") method = "runGeneticAlgorithm";
    else if (algorithm == "Gray Wolf Optimization (GWO)") method = "runGrayWolfOptimization";

    else if (algorithm == "Lin-Kernighan Algorithm (LK)") method = "runLinKernighan";
    else if (algorithm == "Tabu Search Algorithm (TS)") method = "runTabuSearch";
    else if (algorithm == "Cuckoo Search Algorithm (CS)") method = "runCuckooSearch";
    else if (algorithm == "Whale Optimization Algorithm (WOA)") method = "runWhaleOptimization";
    else if (algorithm == "Pelican Optimization Algorithm (POA)") method = "runPelicanOptimization";
    else if (algorithm == "Ecological Cycle Optimizer (ECO)") method = "runEcologicalCycleOptimizer";
    else if (algorithm == "2-Opt Local Search (2-Opt)") method = "runTwoOpt";
    else if (algorithm == "3-Opt Local Search (3-Opt)") method = "runThreeOpt";
    else if (algorithm == "5-Opt Local Search (5-Opt)") method = "runFiveOpt";


    TRACE_MSG("Method selected: " + method);

    connect(m_solverThread, &QThread::started, m_solverWorker, [this, worker = m_solverWorker, method]() {
        if (!worker) return;
        TRACE_MSG("Invoking solver method in background thread: " + method);
        
        std::vector<int> tourCopy;
        {
            QMutexLocker locker(&m_routeMutex);
            tourCopy = m_route; 
        }

        worker->setCandidateCache(m_tspManager.getCandidateCache(m_params.value("global_cand_size", 500).toInt()));
        
        QMetaObject::invokeMethod(worker, method.toStdString().c_str(), 
            Qt::DirectConnection, Q_ARG(std::vector<int>, tourCopy));
    });

    connect(m_solverWorker, &SolverWorker::routeUpdated, this, &GuiController::onRouteUpdated);
    connect(m_solverWorker, &SolverWorker::logMessage, this, &GuiController::newLogMessage);
    connect(m_solverWorker, &SolverWorker::finished, this, &GuiController::onSolverFinished);
    connect(m_solverWorker, &SolverWorker::finished, m_solverThread, &QThread::quit);
    connect(m_solverThread, &QThread::finished, m_solverWorker, &QObject::deleteLater);
    
    TRACE_MSG("Starting solver thread");
    m_solverThread->start();
}

void GuiController::plotInitialSolution() {
    TRACE_SCOPE;
    if (m_tspManager.getCities().size() < 2) {
        emit newLogMessage("Need >= 2 cities."); return;
    }
    cleanupSolver();
    m_route.clear();
    m_previousRoute.clear();
    emit routeChanged();
    emit newLogMessage("Plotting Initial Tour...");
    emit solvingStatusChanged(true);

    if (m_params.value("initial_tour_method").toString() == "christofides") {
        if (m_tspManager.getDelaunayEdges().empty()) {
            computeDelaunay(true);
        }
    }

    m_solverThread = new QThread(this);
    m_solverWorker = new SolverWorker(m_tspManager.getCities());
    m_solverWorker->setParams(m_params);
    m_solverWorker->setDelaunayEdges(m_tspManager.getDelaunayEdges());
    m_solverWorker->setWeightType(m_tspManager.getWeightType());
    m_solverWorker->moveToThread(m_solverThread);

    connect(m_solverThread, &QThread::started, m_solverWorker, [this, worker = m_solverWorker]() {
        if (!worker) return;
        worker->setCandidateCache(m_tspManager.getCandidateCache(m_params.value("global_cand_size", 500).toInt()));
        QMetaObject::invokeMethod(worker, "runInitialTour", Qt::DirectConnection);
    });

    connect(m_solverWorker, &SolverWorker::routeUpdated, this, &GuiController::onRouteUpdated);
    connect(m_solverWorker, &SolverWorker::logMessage, this, &GuiController::newLogMessage);
    connect(m_solverWorker, &SolverWorker::finished, this, &GuiController::onSolverFinished);
    connect(m_solverWorker, &SolverWorker::finished, m_solverThread, &QThread::quit);
    connect(m_solverThread, &QThread::finished, m_solverWorker, &QObject::deleteLater);
    
    m_solverThread->start();
}

void GuiController::setAlgorithmParam(const QString& key, QVariant value) {
    TRACE_SCOPE;
    m_params[key] = value;
    emit paramsChanged();
}

void GuiController::saveSettings() {
    QSettings s("config.ini", QSettings::IniFormat);
    s.setValue("cityPointSize", m_cityPointSize);
    s.setValue("routeLineThickness", m_routeLineThickness);
    s.setValue("cityColor", m_cityColor);
    s.setValue("routeColor", m_routeColor);
    s.setValue("delaunayColor", m_delaunayColor);
    s.setValue("lastAlgorithm", m_lastAlgorithm);
    s.setValue("params", m_params);
    s.setValue("savedRotation", m_savedRotation);
    s.setValue("savedFlipH", m_savedFlipH);
    s.setValue("savedFlipV", m_savedFlipV);
    emit newLogMessage("Settings saved successfully.");
}

void GuiController::loadSettings() {
    QSettings s("config.ini", QSettings::IniFormat);
    if (!s.contains("cityPointSize")) return; // No settings yet
    
    m_cityPointSize = s.value("cityPointSize", 4.0).toDouble();
    m_routeLineThickness = s.value("routeLineThickness", 1.0).toDouble();
    m_cityColor = s.value("cityColor", QColor("#10B981")).value<QColor>();
    m_routeColor = s.value("routeColor", QColor("#38bdf8")).value<QColor>();
    m_delaunayColor = s.value("delaunayColor", QColor("#8b5cf6")).value<QColor>();
    m_lastAlgorithm = s.value("lastAlgorithm", "Iterated Local Search (ILS)").toString();
    m_params = s.value("params", m_params).toMap();
    m_savedRotation = s.value("savedRotation", 0.0).toDouble();
    m_savedFlipH = s.value("savedFlipH", false).toBool();
    m_savedFlipV = s.value("savedFlipV", false).toBool();
    
    emit cityPointSizeChanged();
    emit routeLineThicknessChanged();
    emit cityColorChanged();
    emit routeColorChanged();
    emit delaunayColorChanged();
    emit lastAlgorithmChanged();
    emit paramsChanged();
    emit newLogMessage("Settings loaded.");
}

void GuiController::resetDefaults() {
    QSettings s("config.ini", QSettings::IniFormat);
    s.clear();
    m_cityPointSize = 4.0;
    m_routeLineThickness = 1.0;
    m_cityColor = QColor("#10B981");
    m_routeColor = QColor("#38bdf8");
    m_delaunayColor = QColor("#8b5cf6");
    m_lastAlgorithm = "Iterated Local Search (ILS)";
    m_savedRotation = 0.0;
    m_savedFlipH = false;
    m_savedFlipV = false;
    m_params.clear();
    m_params["initial_tour_method"] = "christofides"; 
    m_params["global_cand_size"] = 500;
    
    emit cityPointSizeChanged();
    emit routeLineThicknessChanged();
    emit cityColorChanged();
    emit routeColorChanged();
    emit delaunayColorChanged();
    emit lastAlgorithmChanged();
    emit paramsChanged();
    emit newLogMessage("Settings reset to defaults.");
}

void GuiController::stopSolver() {
    TRACE_SCOPE;
    if (m_solverWorker) { m_solverWorker->requestStop(); emit newLogMessage("Stop requested..."); }
}

void GuiController::onRouteUpdated(const std::vector<int>& route, double distance) {
    if ((int)route.size() != (int)m_tspManager.getCities().size()) return;
    {
        QMutexLocker locker(&m_routeMutex);
        m_previousRoute = m_route;
        m_route = route;
    }
    emit routeChanged();
    emit metricsUpdated(distance, 0);
}

void GuiController::onSolverFinished(double d, int ms) {
    TRACE_SCOPE;
    {
        QMutexLocker locker(&m_routeMutex);
        m_previousRoute.clear();
    }
    emit newLogMessage(QString("Distance: %1 | %2 ms").arg(d,0,'f',2).arg(ms));
    emit solvingStatusChanged(false); emit metricsUpdated(d, ms);
}

void GuiController::cleanupSolver() {
    TRACE_SCOPE;
    if (m_solverWorker) {
        TRACE_MSG("Requesting stop for worker");
        m_solverWorker->requestStop();
    }
    
    if (m_solverThread) {
        TRACE_MSG("Waiting for thread to terminate");
        if (m_solverThread->isRunning()) {
            m_solverThread->quit();
            if (!m_solverThread->wait(3000)) {
                TRACE_MSG("Thread wait timed out, forcing termination");
                m_solverThread->terminate();
                m_solverThread->wait();
            }
        }
        m_solverThread->deleteLater();
        m_solverThread = nullptr;
    }
    m_solverWorker = nullptr;
}

void GuiController::addCity(double x, double y) {
    TRACE_SCOPE;
    cleanupSolver();
    m_tspManager.addCity(x, y); 
    m_route.clear();
    m_previousRoute.clear();
    emit cityAdded(x, y);
    emit dataChanged(); emit routeChanged();
    if (m_showDelaunay) computeDelaunay(false);
}

void GuiController::removeCity(int index) {
    TRACE_SCOPE;
    if (index < 0 || index >= (int)m_tspManager.getCities().size()) return;
    cleanupSolver();
    const auto& city = m_tspManager.getCities().at(index);
    double x = city.x, y = city.y;
    m_tspManager.removeCity(index); 
    m_route.clear();
    m_previousRoute.clear();
    emit cityRemoved(x, y);
    emit dataChanged(); emit routeChanged();
    if (m_showDelaunay) computeDelaunay(false);
}

void GuiController::updateCityPosition(int index, double x, double y) {
    m_tspManager.updateCity(index, x, y);
    if (m_showDelaunay) computeDelaunay(true); // Silent re-calculation during move
    emit dataChanged();
}

void GuiController::clearCities() {
    TRACE_SCOPE;
    cleanupSolver(); m_tspManager.clear(); m_route.clear(); m_previousRoute.clear();
    emit dataChanged();
    emit routeChanged(); emit viewResetNeeded(); emit newLogMessage("Cleared.");
}

void GuiController::rotateCities(double angleDegrees) {
    TRACE_SCOPE;
    m_tspManager.rotateCities(angleDegrees);
    m_savedRotation = fmod(m_savedRotation + angleDegrees, 360.0);
    emit dataChanged();
    emit routeChanged();
    emit viewResetNeeded();
    if (m_showDelaunay) computeDelaunay(true);
}
void GuiController::flipCities(bool horizontal) {
    TRACE_SCOPE;
    m_tspManager.flipCities(horizontal);
    if (horizontal) m_savedFlipH = !m_savedFlipH;
    else m_savedFlipV = !m_savedFlipV;
    emit dataChanged();
    emit routeChanged();
    emit viewResetNeeded();
    if (m_showDelaunay) computeDelaunay(true);
}
void GuiController::computeDelaunay(bool silent) {
    TRACE_SCOPE;
    const auto& cities = m_tspManager.getCities();
    if (cities.size() < 3) return;

    if (!silent) emit newLogMessage("Computing Delaunay Triangulation...");
    QElapsedTimer timer;
    timer.start();

    m_tspManager.computeDelaunay();

    if (!silent) {
        qint64 elapsed = timer.elapsed();
        emit newLogMessage(QString("Delaunay complete: %1 segments in %2 ms").arg(m_tspManager.getDelaunayEdges().size()/2).arg(elapsed));
    }
    
    m_showDelaunay = true;
    emit showDelaunayChanged();
    emit dataChanged(); // Trigger repaint
}

void GuiController::resetTSPStudio() {
    TRACE_SCOPE;
    cleanupSolver();
    {
        QMutexLocker locker(&m_routeMutex);
        m_route.clear();
        m_previousRoute.clear();
    }
    emit routeChanged();
    emit metricsUpdated(0, 0);
    emit newLogMessage("TSP Studio Reset. Next run will start from initial tour.");
}

void GuiController::saveRoute(const QString& fileUrl) {
    TRACE_SCOPE;
    if (m_route.empty()) { emit newLogMessage("[WARN] No route to save."); return; }
    QString path = QUrl(fileUrl).toLocalFile();
    if (path.isEmpty()) path = fileUrl;
    if (m_tspManager.saveTour(path, m_route)) {
        emit newLogMessage("Tour saved to: " + path);
    } else {
        emit newLogMessage("[ERROR] Could not save tour.");
    }
}

void GuiController::loadRoute(const QString& fileUrl) {
    TRACE_SCOPE;
    cleanupSolver();
    QString path = QUrl(fileUrl).toLocalFile();
    if (path.isEmpty()) path = fileUrl;
    std::vector<int> loaded = m_tspManager.loadTour(path);
    if (!loaded.empty()) {
        {
            QMutexLocker locker(&m_routeMutex);
            m_route = loaded;
            m_previousRoute.clear();
        }
        emit routeChanged();
        // Recalculate distance for metrics
        double d = 0;
        const auto& cities = m_tspManager.getCities();
        // Since we don't have SolverWorker::totalDistance easy here, we define it or use TSPStudioManager.
        // Actually, we'll just use a small helper or rely on the UI updating on next move.
        // I'll calculate it manually here for immediate feedback.
        auto dist_local = [&](int i, int j) -> double {
            const auto& c1 = cities[i]; const auto& c2 = cities[j];
            double dx = c1.x-c2.x, dy = c1.y-c2.y;
            return std::sqrt(dx*dx + dy*dy); // Approximation for display
        };
        for(size_t i=0; i<loaded.size(); ++i) d += dist_local(loaded[i], loaded[(i+1)%loaded.size()]);
        
        emit metricsUpdated(d, 0);
        emit newLogMessage("Tour loaded: " + QString::number(loaded.size()) + " cities.");
    } else {
        emit newLogMessage("[ERROR] Could not load tour or tour invalid.");
    }
}

bool GuiController::tracingEnabled() const { return Tracer::isEnabled(); }

void GuiController::setTracingEnabled(bool e) {
    if (Tracer::isEnabled() != e) {
        Tracer::setEnabled(e);
        emit tracingEnabledChanged();
    }
}
