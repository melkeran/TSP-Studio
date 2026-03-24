/**
 * @file TSPStudioManager.h
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#pragma once

#include <vector>
#include <QString>
#include <QVariantList>
#include <memory>
#include "core/City.h"
#include "core/KDTree.h"

class TSPStudioManager {
public:
    enum EdgeWeightType {
        EUC_2D,
        GEOM,
        GEO,
        ATT,
        CEIL_2D
    };

    TSPStudioManager() : m_weightType(EUC_2D) {}

    // Parses standard .tsp files (EUC_2D etc)
    bool loadTSPFile(const QString& filePath);
    bool saveTour(const QString& filePath, const std::vector<int>& tour);
    std::vector<int> loadTour(const QString& filePath);
    
    // Exposes cities to the QML Javascript environment
    QVariantList getCitiesAsVariantList() const;
    
    // Exposes cities to the C++ Solvers
    const std::vector<City>& getCities() const { return m_cities; }

    void addCity(double x, double y);
    void removeCity(int index);
    void updateCity(int index, double x, double y);
    void clear();

    const std::vector<uint32_t>& getDelaunayEdges() const { return m_delaunayEdges; }
    void setDelaunayEdges(std::vector<uint32_t> edges) { m_delaunayEdges = std::move(edges); }

    void computeDelaunay();
    void rotateCities(double angleDegrees);
    void flipCities(bool horizontal);
    
    EdgeWeightType getWeightType() const { return m_weightType; }
    
    typedef CDT::KDTree::KDTree<double> KDTree;
    const KDTree* getKDTree() const { return m_kdTree.get(); }
    void updateKDTree();
    
    // Persistent Candidate Cache
    const std::vector<std::vector<int>>& getCandidateCache(int kSize);
    void invalidateCandidateCache() { m_candidateCache.clear(); m_lastK = -1; }

private:
    bool loadFromBuffer(const char* data, qint64 size);
    std::vector<City> m_cities;
    std::vector<uint32_t> m_delaunayEdges;
    QString m_name;
    EdgeWeightType m_weightType;
    std::unique_ptr<KDTree> m_kdTree;
    std::vector<std::vector<int>> m_candidateCache;
    int m_lastK = -1;
};
