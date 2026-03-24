/**
 * @file SolverWorker.h
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#pragma once

#include <QObject>
#include <QThread>
#include <vector>
#include <random>
#include <atomic>
#include <QVariantMap>
#include <QElapsedTimer>
#include "core/City.h"
#include "core/TSPStudioManager.h"

class SolverWorker : public QObject {
    Q_OBJECT
public:
    explicit SolverWorker(const std::vector<City>& cities, QObject* parent = nullptr);

    void setParams(const QVariantMap& p) { m_params = p; }
    void requestStop() { m_stopRequested = true; }
    void setDelaunayEdges(const std::vector<uint32_t>& edges) { m_delaunayEdges = edges; }
    void setCandidateCache(const std::vector<std::vector<int>>& cache) { m_candCache = cache; }
    void setWeightType(TSPStudioManager::EdgeWeightType type) { m_weightType = type; }

public slots:
    void runTwoOpt(const std::vector<int>& initialTour = {});
    void runThreeOpt(const std::vector<int>& initialTour = {});
    void runFiveOpt(const std::vector<int>& initialTour = {});

    void runCuckooSearch(const std::vector<int>& initialTour = {});
    void runGeneticAlgorithm(const std::vector<int>& initialTour = {});
    void runGrayWolfOptimization(const std::vector<int>& initialTour = {});
    void runGuidedLocalSearch(const std::vector<int>& initialTour = {});
    void runIteratedLocalSearch(const std::vector<int>& initialTour = {});
    void runLinKernighan(const std::vector<int>& initialTour = {});
    void runTabuSearch(const std::vector<int>& initialTour = {});
    void runWhaleOptimization(const std::vector<int>& initialTour = {});
    void runPelicanOptimization(const std::vector<int>& initialTour = {});
    void runEcologicalCycleOptimizer(const std::vector<int>& initialTour = {});
    void runInitialTour();


signals:
    void routeUpdated(const std::vector<int>& route, double distance);
    void logMessage(const QString& msg);
    void finished(double finalDistance, int elapsedMs);

private:
    std::vector<City> m_cities;
    std::vector<uint32_t> m_delaunayEdges;
    std::atomic<bool> m_stopRequested{false};
    std::mt19937 m_rng;
    QVariantMap m_params;
    mutable std::vector<std::vector<int>> m_candCache;
    QElapsedTimer m_updateTimer;
    double m_bestDistanceEmitted{1e18};
    TSPStudioManager::EdgeWeightType m_weightType{TSPStudioManager::EUC_2D};

    double dist(int i, int j) const;
    double totalDistance(const std::vector<int>& route) const;
    void emitRouteUpdated(const std::vector<int>& route, double distance, bool force = false);
    std::vector<int> nearestNeighborTour();
    std::vector<int> christofidesTour();
    std::vector<int> generateInitialTour();

    // Local Search Helpers
    bool optimizeSequentialKOpt(std::vector<int>& route, double& bestDist, int maxK, bool useDLB);
    bool optimizeTwoOpt(std::vector<int>& route, double& bestDist, bool useCandidates = false, bool useDLB = false);
    bool optimizeThreeOpt(std::vector<int>& route, double& bestDist, bool useCandidates = false, bool useDLB = false);
    bool optimizeFiveOpt(std::vector<int>& route, double& bestDist, bool useCandidates = false, bool useDLB = false);

    std::vector<std::vector<int>> getCandidates(int size) const;
    std::vector<int> crossoverOX(const std::vector<int>& p1, const std::vector<int>& p2);
    bool performILS(std::vector<int>& route, double& bestDist, int steps, double kickFract);

    struct Edge {
        int u, v;
        double w;
        bool operator<(const Edge& other) const { return w < other.w; }
    };
    std::vector<Edge> getMstEdges() const;
};
