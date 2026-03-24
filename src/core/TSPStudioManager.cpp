/**
 * @file TSPStudioManager.cpp
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#include "core/TSPStudioManager.h"

#include <QFile>
#include <QTextStream>
#include <QVariantMap>
#include <QRegularExpression>
#include "core/Tracer.h"

// Third-party CDT library
#include <CDT.h>
#include <Triangulation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

bool TSPStudioManager::loadTSPFile(const QString& filePath) {
    TRACE_SCOPE;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        TRACE_MSG("Failed to open file: " + filePath);
        return false;
    }
    m_cities.clear();
    m_delaunayEdges.clear();
    m_weightType = EUC_2D; // Reset to default
    invalidateCandidateCache();

    qint64 fileSize = file.size();
    if (fileSize <= 0) return false;

    // Memory map the file for high-performance access
    uchar* data = file.map(0, fileSize);
    if (!data) {
        TRACE_MSG("Memory map failed, falling back to traditional read");
        // Simple fallback for very large files or systems without mapping support
        QByteArray all = file.readAll();
        if (all.isEmpty()) return false;
        // We'll process from the byte array instead
        return loadFromBuffer(all.constData(), all.size());
    }

    bool success = loadFromBuffer(reinterpret_cast<const char*>(data), fileSize);
    file.unmap(data);
    file.close();
    return success;
}

// Cross-platform case-insensitive string compare
static int compareCaseInsensitive(const char* s1, const char* s2, size_t n) {
#ifdef _MSC_VER
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

// Internal helper for fast parsing
bool TSPStudioManager::loadFromBuffer(const char* data, qint64 size) {
    TRACE_SCOPE;
    const char* ptr = data;
    const char* end = data + size;

    auto skipSpaces = [&ptr, end]() {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
    };

    auto skipToNextLine = [&ptr, end]() {
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr < end && *ptr == '\n') ++ptr;
    };

    auto matchToken = [&ptr, end](const char* token) -> bool {
        size_t len = strlen(token);
        if (ptr + len > end) return false;
        if (compareCaseInsensitive(ptr, token, len) == 0) {
            ptr += len;
            return true;
        }
        return false;
    };

    int expectedDimension = -1;
    bool readingNodes = false;
    m_weightType = EUC_2D;

    while (ptr < end) {
        // Skip leading whitespace of the line
        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')) ++ptr;
        if (ptr >= end) break;

        if (!readingNodes) {
            if (matchToken("DIMENSION")) {
                while (ptr < end && (*ptr < '0' || *ptr > '9') && *ptr != '\n') ++ptr;
                if (ptr < end && *ptr >= '0' && *ptr <= '9') {
                    char* next;
                    expectedDimension = static_cast<int>(strtol(ptr, &next, 10));
                    ptr = next;
                    if (expectedDimension > 0) m_cities.reserve(expectedDimension);
                }
                skipToNextLine();
            } else if (matchToken("EDGE_WEIGHT_TYPE")) {
                while (ptr < end && *ptr != ':' && !isgraph(*ptr)) ++ptr;
                if (ptr < end && *ptr == ':') ++ptr;
                while (ptr < end && !isgraph(*ptr)) ++ptr;
                if (matchToken("GEOM")) m_weightType = GEOM;
                else if (matchToken("GEO")) m_weightType = GEO;
                else if (matchToken("ATT")) m_weightType = ATT;
                else if (matchToken("CEIL_2D")) m_weightType = CEIL_2D;
                else m_weightType = EUC_2D;
                skipToNextLine();
            } else if (matchToken("NODE_COORD_SECTION")) {
                readingNodes = true;
                skipToNextLine();
            } else if (matchToken("EOF")) {
                break;
            } else {
                skipToNextLine();
            }
        }
 else {
            if (matchToken("EOF") || matchToken("TOUR_SECTION")) break;

            auto parseNumber = [&](const char* s, const char** next) -> double {
                const char* cur = s;
                while (cur < end && (*cur == '-' || *cur == '.' || (*cur >= '0' && *cur <= '9') || *cur == 'e' || *cur == 'E' || *cur == '+')) ++cur;
                *next = cur;
                return QByteArray::fromRawData(s, static_cast<int>(cur - s)).toDouble();
            };

            const char* next;
            // Try to parse up to 3 numbers (ID, X, Y)
            double val1 = parseNumber(ptr, &next);
            if (ptr == next) { 
                skipToNextLine();
                continue;
            }
            ptr = next;

            skipSpaces();
            double val2 = parseNumber(ptr, &next);
            if (ptr == next) {
                skipToNextLine();
                continue;
            }
            ptr = next;

            skipSpaces();
            double val3 = parseNumber(ptr, &next);
            if (ptr == next) {
                // Only 2 numbers: val1=X, val2=Y
                m_cities.push_back({static_cast<int>(m_cities.size() + 1), val1, val2});
            } else {
                // 3 numbers: val1=ID, val2=X, val3=Y
                m_cities.push_back({static_cast<int>(val1), val2, val3});
                ptr = next;
            }
            skipToNextLine();
        }
    }

    // Deduplicate cities if needed (O(N log N))
    if (!m_cities.empty()) {
        TRACE_MSG(QString("Finalizing %1 cities...").arg(m_cities.size()));
        std::sort(m_cities.begin(), m_cities.end(), [](const City& a, const City& b){
            if (std::abs(a.x - b.x) > 1e-9) return a.x < b.x;
            return a.y < b.y;
        });
        m_cities.erase(std::unique(m_cities.begin(), m_cities.end(), [](const City& a, const City& b){
            return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9;
        }), m_cities.end());
    }

    updateKDTree();
    invalidateCandidateCache(); // Invalidate cache after loading new cities
    return !m_cities.empty();
}

void TSPStudioManager::updateKDTree() {
    TRACE_SCOPE;
    m_kdTree.reset(new KDTree());
    for (size_t i = 0; i < m_cities.size(); ++i) {
        m_kdTree->insert(static_cast<CDT::VertInd>(i), m_cities);
    }
}


QVariantList TSPStudioManager::getCitiesAsVariantList() const {
    TRACE_SCOPE;
    QVariantList list;
    list.reserve(static_cast<int>(m_cities.size()));
    for (const auto& city : m_cities) {
        QVariantMap map;
        map["id"] = city.id;
        map["x"] = city.x;
        map["y"] = city.y;
        list.append(map);
    }
    return list;
}

void TSPStudioManager::updateCity(int index, double x, double y) { if (index >= 0 && index < (int)m_cities.size()) { m_cities[index].x = x; m_cities[index].y = y; invalidateCandidateCache(); updateKDTree(); } }
void TSPStudioManager::addCity(double x, double y) { m_cities.emplace_back(static_cast<int>(m_cities.size() + 1), x, y); invalidateCandidateCache(); updateKDTree(); }
void TSPStudioManager::removeCity(int index) { if (index >= 0 && index < (int)m_cities.size()) { m_cities.erase(m_cities.begin() + index); invalidateCandidateCache(); updateKDTree(); } }
void TSPStudioManager::clear() {
    TRACE_SCOPE;
    m_cities.clear();
    m_delaunayEdges.clear();
    invalidateCandidateCache();
    m_kdTree.reset();
    m_name.clear();
}

void TSPStudioManager::computeDelaunay() {
    TRACE_SCOPE;
    if (m_cities.size() < 3) return;

    try {
        std::vector<CDT::V2d<double>> vertices;
        vertices.reserve(m_cities.size());
        for (const auto& c : m_cities) vertices.push_back({c.x, c.y});

        CDT::Triangulation<double> cdt;
        try {
            cdt.insertVertices(vertices);
        } catch (...) {
            TRACE_MSG("Standard triangulation failed, retrying with fresh object and perturbation...");
            CDT::Triangulation<double> cdtRetry;
            for (auto& v : vertices) {
                v.x += (double(rand() % 1000) - 500.0) * 1e-7;
                v.y += (double(rand() % 1000) - 500.0) * 1e-7;
            }
            cdtRetry.insertVertices(vertices);
            cdt = std::move(cdtRetry);
        }
        
        cdt.eraseSuperTriangle();

        const size_t nc = m_cities.size();
        std::vector<uint32_t> rawEdges;
        rawEdges.reserve(cdt.triangles.size() * 6);

        for (const auto& tri : cdt.triangles) {
            for (int i = 0; i < 3; ++i) {
                std::size_t vi1 = tri.vertices[i];
                std::size_t vi2 = tri.vertices[(i + 1) % 3];
                if (vi1 < nc && vi2 < nc) {
                    rawEdges.push_back(static_cast<uint32_t>(std::min(vi1, vi2)));
                    rawEdges.push_back(static_cast<uint32_t>(std::max(vi1, vi2)));
                }
            }
        }

        struct Edge {
            uint32_t a, b;
            bool operator<(const Edge& o) const { return a < o.a || (a == o.a && b < o.b); }
            bool operator==(const Edge& o) const { return a == o.a && b == o.b; }
        };
        
        std::vector<Edge> uniqueEdges;
        uniqueEdges.reserve(rawEdges.size() / 2);
        for (size_t i = 0; i < rawEdges.size(); i += 2) {
            uniqueEdges.push_back({rawEdges[i], rawEdges[i+1]});
        }
        std::sort(uniqueEdges.begin(), uniqueEdges.end());
        uniqueEdges.erase(std::unique(uniqueEdges.begin(), uniqueEdges.end()), uniqueEdges.end());

        m_delaunayEdges.clear();
        m_delaunayEdges.reserve(uniqueEdges.size() * 2);
        for (const auto& e : uniqueEdges) {
            m_delaunayEdges.push_back(e.a);
            m_delaunayEdges.push_back(e.b);
        }
        invalidateCandidateCache(); // Invalidate cache after Delaunay edges change
    } catch (...) {
        TRACE_MSG("Delaunay Triangulation aborted");
    }
}

void TSPStudioManager::rotateCities(double angleDegrees) {
    if (m_cities.empty()) return;
    const double PI = 3.14159265358979323846;
    double rad = angleDegrees * PI / 180.0;
    double cosA = std::cos(rad);
    double sinA = std::sin(rad);

    double centerX = 0, centerY = 0;
    for (const auto& c : m_cities) {
        centerX += c.x;
        centerY += c.y;
    }
    double n = static_cast<double>(m_cities.size());
    centerX /= n;
    centerY /= n;

    for (auto& c : m_cities) {
        double dx = c.x - centerX;
        double dy = c.y - centerY;
        double newX = centerX + (dx * cosA - dy * sinA);
        double newY = centerY + (dx * sinA + dy * cosA);
        c.x = newX;
        c.y = newY;
    }
    m_delaunayEdges.clear();
    updateKDTree();
}

void TSPStudioManager::flipCities(bool horizontal) {
    if (m_cities.empty()) return;
    double minX = m_cities[0].x, maxX = m_cities[0].x;
    double minY = m_cities[0].y, maxY = m_cities[0].y;
    for (const auto& c : m_cities) {
        minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
    }
    double midX = (minX + maxX) * 0.5, midY = (minY + maxY) * 0.5;
    for (auto& c : m_cities) {
        if (horizontal) c.x = midX - (c.x - midX);
        else c.y = midY - (c.y - midY);
    }
    m_delaunayEdges.clear();
    updateKDTree();
}

bool TSPStudioManager::saveTour(const QString& filePath, const std::vector<int>& tour) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "NAME : " << m_name << ".tour\n";
    out << "TYPE : TOUR\n";
    out << "DIMENSION : " << tour.size() << "\n";
    out << "TOUR_SECTION\n";
    for (int idx : tour) out << (idx + 1) << "\n";
    out << "-1\nEOF\n";
    return true;
}

std::vector<int> TSPStudioManager::loadTour(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&file);
    std::vector<int> tour;
    bool inSection = false;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("DIMENSION")) {
            int dim = line.section(':', 1).trimmed().toInt();
            if (dim > 0) tour.reserve(dim);
        } else if (line == "TOUR_SECTION") inSection = true;
        else if (inSection) {
            int val = line.toInt();
            if (val == -1 || line == "EOF") break;
            if (val > 0 && val <= (int)m_cities.size()) tour.push_back(val - 1);
        }
    }
    return tour;
}

const std::vector<std::vector<int>>& TSPStudioManager::getCandidateCache(int kSize) {
    if (m_lastK == kSize && !m_candidateCache.empty()) return m_candidateCache;

    int n = static_cast<int>(m_cities.size());
    m_candidateCache.clear();
    m_candidateCache.resize(n);
    if (n < 2) return m_candidateCache;

    if (!m_kdTree) updateKDTree();
    int k = std::min(kSize, n - 1);
    m_lastK = kSize;

    #pragma omp parallel for schedule(dynamic, 100)
    for (int i = 0; i < n; ++i) {
        auto result = m_kdTree->kNearest(CDT::V2d<double>(m_cities[i].x, m_cities[i].y), k, m_cities, i);
        m_candidateCache[i].reserve(k + 4);
        for (auto idx : result) m_candidateCache[i].push_back((int)idx);
    }

    if (!m_delaunayEdges.empty()) {
        for (size_t i = 0; i < m_delaunayEdges.size(); i += 2) {
            int u = (int)m_delaunayEdges[i];
            int v = (int)m_delaunayEdges[i + 1];
            if (u >= 0 && u < n && v >= 0 && v < n) {
                m_candidateCache[u].push_back(v);
                m_candidateCache[v].push_back(u);
            }
        }
    }

    // Parallel sorting pass
    #pragma omp parallel for schedule(dynamic, 200)
    for (int i = 0; i < n; ++i) {
        std::sort(m_candidateCache[i].begin(), m_candidateCache[i].end());
        m_candidateCache[i].erase(std::unique(m_candidateCache[i].begin(), m_candidateCache[i].end()), m_candidateCache[i].end());
    }

    return m_candidateCache;
}
