/**
 * @file SolverWorker.cpp
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#include "solvers/SolverWorker.h"

#include "core/KDTree.h"
#include <cmath>
#include <algorithm>
#include <QElapsedTimer>
#include <random>
#include <chrono>
#include <numeric>
#include <stack>
#include <deque>
#include <mutex>
#include "core/Tracer.h"

SolverWorker::SolverWorker(const std::vector<City>& cities, QObject* parent)
    : QObject(parent), m_cities(cities) {
    TRACE_SCOPE;
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    m_rng.seed(static_cast<unsigned int>(seed));
    m_updateTimer.start();
}

void SolverWorker::emitRouteUpdated(const std::vector<int>& route, double distance, bool force) {
    if (!force && distance > m_bestDistanceEmitted + 1e-9) return;

    // Tour Validity check for stability/debugging
    int n = static_cast<int>(m_cities.size());
    if (route.size() == (size_t)n) {
        static std::vector<bool> seen; seen.assign(n, false);
        bool valid = true;
        for (int c : route) {
            if (c < 0 || c >= n || seen[c]) { valid = false; break; }
            seen[c] = true;
        }
        if (!valid) {
            TRACE_MSG("CRITICAL: Invalid tour detected (duplicates or out of range)!");
            return; 
        }
    }

    if (force || !m_updateTimer.isValid() || m_updateTimer.elapsed() > 100) {
        m_bestDistanceEmitted = distance;
        emit routeUpdated(route, distance);
        m_updateTimer.restart();
    }
}

double SolverWorker::dist(int i, int j) const {
    const auto& c1 = m_cities[i];
    const auto& c2 = m_cities[j];
    double dx = c1.x - c2.x;
    double dy = c1.y - c2.y;

    if (m_weightType == TSPStudioManager::GEO || m_weightType == TSPStudioManager::GEOM) {
        auto to_geo = [](double x) {
            double deg = (int)x;
            double min = x - deg;
            return 3.14159265358979323846 * (deg + 5.0 * min / 3.0) / 180.0;
        };
        double lat1 = to_geo(c1.y), lon1 = to_geo(c1.x);
        double lat2 = to_geo(c2.y), lon2 = to_geo(c2.x);
        double q1 = std::cos(lon1 - lon2), q2 = std::cos(lat1 - lat2), q3 = std::cos(lat1 + lat2);
        double RRR = (m_weightType == TSPStudioManager::GEOM) ? 6378388.0 : 6378.388;
        return (double)((int)(RRR * std::acos(0.5 * ((1.0 + q1) * q2 - (1.0 - q1) * q3)) + 1.0));
    }
    
    // For EUC_2D, use high-precision double. Only TSPLIB-specific rounding if explicitly required.
    // We'll keep it double to prevent 'zero-distance' errors on small-scale maps.
    double d = std::sqrt(dx * dx + dy * dy);
    if (m_weightType == TSPStudioManager::CEIL_2D) return std::ceil(d);
    return d;
}

double SolverWorker::totalDistance(const std::vector<int>& route) const {
    TRACE_SCOPE;
    double d = 0.0;
    int n = static_cast<int>(route.size());
    if (n < 2) return 0.0;
    for (int i = 0; i < n; ++i) {
        d += dist(route.at(i), route.at((i + 1) % n));
    }
    return d;
}

std::vector<int> SolverWorker::nearestNeighborTour() {
    TRACE_SCOPE;
    int n = static_cast<int>(m_cities.size());
    if (n < 2) return {0};

    TRACE_MSG("Using persistent candidate cache from TSPStudioManager");
    if (m_candCache.empty()) {
        emit logMessage("No candidate cache provided. Nearest Neighbor might be slow.");
    }

    std::vector<int> tour; tour.reserve(n);
    std::vector<bool> visited(n, false);
    tour.push_back(0); visited[0] = true;

    TRACE_MSG("Starting NN construction");
    for (int step = 1; step < n && !m_stopRequested; ++step) {
        int last = tour.back();
        int best = -1;

        for (int neighbor : m_candCache[last]) {
            if (m_stopRequested) break;
            if (!visited[neighbor]) { best = neighbor; break; }
        }

        if (best == -1 && !m_stopRequested) {
            double bestD = 1e18;
            for (int j = 0; j < n; ++j) {
                if (m_stopRequested) break;
                if (!visited[j]) {
                    double d = dist(last, j);
                    if (d < bestD) { bestD = d; best = j; }
                }
            }
        }

        if (best != -1 && !m_stopRequested) {
            tour.push_back(best);
            visited[best] = true;
        } else break;

        // Visual progress for NN construction
        if (step % 100 == 0 || (n < 1000 && step % 20 == 0)) {
            std::vector<int> proc = tour;
            proc.reserve(n);
            for(int j=0; j<n && proc.size() < (size_t)n; j++) {
                if (!visited[j]) proc.push_back(j);
            }
            if (proc.size() == (size_t)n) {
                emitRouteUpdated(proc, 0.0, true);
                QThread::yieldCurrentThread();
            }
        }
    }
    if (m_stopRequested || (int)tour.size() < n) {
        TRACE_MSG("NN Construction interrupted or failed");
        return {};
    }
    TRACE_MSG("NN Construction complete");
    return tour;
}

bool SolverWorker::optimizeTwoOpt(std::vector<int>& route, double& bestDist, bool useCandidates, bool useDLB) {
    TRACE_SCOPE;
    int n = static_cast<int>(route.size());
    if (n < 4) return false;

    int totalN = static_cast<int>(m_cities.size());
    std::vector<int> pos(totalN, -1);
    auto refreshPos = [&](){ for(int i=0; i<n; i++) pos[route[i]]=i; };
    refreshPos();

    if (useCandidates && m_candCache.empty()) {
        int candSize = m_params.value("global_cand_size", 500).toInt();
        m_candCache = getCandidates(candSize);
    }

    std::vector<bool> dlb(n, false);
    bool totalImproved = false;
    bool improved = true;

    auto circularReverse = [&](int s, int e) {
        int rlen = (e - s + n) % n + 1;
        int half = rlen / 2;
        for (int i = 0; i < half; ++i) {
            int i1 = (s + i) % n, i2 = (e - i + n) % n;
            std::swap(route[i1], route[i2]);
            pos[route[i1]] = i1; pos[route[i2]] = i2;
        }
    };

    while (improved && !m_stopRequested) {
        improved = false;
        for (int i = 0; i < n && !m_stopRequested; i++) {
            if (useDLB && dlb[i]) continue;

            int a = route[i], b = route[(i + 1) % n];
            double d_ab = dist(a, b);
            bool foundForI = false;

            if (useCandidates) {
                for (int c : m_candCache[a]) {
                    int pc = pos[c];
                    if (pc == i || pc == (i + 1) % n || ((pc + 1) % n) == i) continue;
                    int pd = (pc + 1) % n, d = route[pd];
                    double gain = (d_ab + dist(c, d)) - (dist(a, c) + dist(b, d));
                    if (gain > 1e-7) {
                        circularReverse((i + 1) % n, pc);
                        bestDist -= gain; improved = true; totalImproved = true;
                        if (useDLB) { dlb[i] = dlb[(i + 1) % n] = dlb[pc] = dlb[pd] = false; }
                        foundForI = true; emitRouteUpdated(route, bestDist, false);
                        QThread::yieldCurrentThread(); break;
                    }
                }
            } else {
                for (int j = 0; j < n && !m_stopRequested; j++) {
                    if (j == i || j == (i + 1) % n || ((j + 1) % n) == i) continue;
                    int pd = (j + 1) % n, d = route[pd];
                    double gain = (d_ab + dist(route[j], d)) - (dist(a, route[j]) + dist(b, d));
                    if (gain > 1e-7) {
                        circularReverse((i + 1) % n, j);
                        bestDist -= gain; improved = true; totalImproved = true;
                        if (useDLB) { dlb[i] = dlb[(i + 1) % n] = dlb[j] = dlb[pd] = false; }
                        foundForI = true; emitRouteUpdated(route, bestDist, false);
                        QThread::yieldCurrentThread(); break;
                    }
                }
            }

            if (useDLB && !foundForI) dlb[i] = true;
        }
    }
    bestDist = totalDistance(route); // Safety recalibration
    return totalImproved;
}

std::vector<int> SolverWorker::generateInitialTour() {
    TRACE_SCOPE;

    QString method = m_params.value("initial_tour_method", "nearest_neighbor").toString();

    if (method == "christofides") {
        TRACE_MSG("Using Christofides for initial tour");
        return christofidesTour();
    } else if (method == "nearest_neighbor") {
        TRACE_MSG("Using Nearest Neighbor for initial tour");
        return nearestNeighborTour();
    } else if (method == "random") {
        TRACE_MSG("Using Random Shuffle for initial tour");
        int n = static_cast<int>(m_cities.size());
        std::vector<int> tour(n);
        std::iota(tour.begin(), tour.end(), 0);
        std::shuffle(tour.begin(), tour.end(), m_rng);
        return tour;
    }

    // Fallback for old checkbox
    if (m_params.value("use_nearest_neighbor", true).toBool()) {
        TRACE_MSG("Fallback: Using NN for initial tour");
        return nearestNeighborTour();
    }

    int n = static_cast<int>(m_cities.size());
    std::vector<int> tour(n);
    std::iota(tour.begin(), tour.end(), 0);
    std::shuffle(tour.begin(), tour.end(), m_rng);
    return tour;
}

bool SolverWorker::optimizeSequentialKOpt(std::vector<int>& route, double& bestDist, int maxK, bool useDLB) {
    int n = static_cast<int>(route.size());
    if (n < maxK * 2) return false;

    std::vector<int> pos(n);
    // Helper to refresh position map for a given route
    auto refreshPos = [&](const std::vector<int>& r) {
        for (int i = 0; i < n; i++) pos[r[i]] = i;
    };
    refreshPos(route);

    std::vector<bool> dlb(n, false);
    bool totalImproved = false;
    bool improved = true;

    // Helper to safely flip a segment in a specific route and position map
    auto flip = [&](std::vector<int>& r, std::vector<int>& p, int i, int j) {
        if (i == j) return;
        int len = (j - i + n) % n + 1;
        for (int k = 0; k < len / 2; ++k) {
            int idx1 = (i + k) % n;
            int idx2 = (j - k + n) % n;
            std::swap(r[idx1], r[idx2]);
            p[r[idx1]] = idx1;
            p[r[idx2]] = idx2;
        }
    };

    while (improved && !m_stopRequested) {
        improved = false;

        for (int i1 = 0; i1 < n && !m_stopRequested; i1++) {
            if (useDLB && dlb[i1]) continue;
            bool foundForI = false;

            // To explore K depth safely, we maintain a tentative route state
            std::vector<int> t_route = route;
            std::vector<int> t_pos = pos;

            int t1 = t_route[i1];
            int i2 = (i1 + 1) % n;
            int curr_tail = t_route[i2];

            double gain_so_far = dist(t1, curr_tail);
            double best_path_gain = 0;
            std::vector<int> best_route_state;

            // Dive up to maxK depth
            for (int k = 2; k <= maxK; ++k) {
                bool advanced = false;
                int best_t3 = -1, best_i3 = -1;
                double best_step_gain = -1e18;

                // Find the best candidate to continue the sequence
                for (int t3 : m_candCache[curr_tail]) {
                    int i3 = t_pos[t3];

                    // Prevent immediate backtracking
                    if (i3 == t_pos[t1] || i3 == t_pos[curr_tail]) continue;

                    double step_gain = gain_so_far - dist(curr_tail, t3);
                    if (step_gain <= 0) continue; // Positive Gain Pruning (The Gatekeeper)

                    int i4 = (i3 + 1) % n;
                    int t4 = t_route[i4];

                    // Check if closing the tour right here yields an improvement
                    double closing_gain = step_gain + dist(t3, t4) - dist(t4, t1);

                    if (closing_gain > best_step_gain) {
                        best_step_gain = closing_gain;
                        best_t3 = t3;
                        best_i3 = i3;
                    }
                }

                // If we found a viable sequence extension
                if (best_t3 != -1) {
                    int i4 = (best_i3 + 1) % n;
                    int t4 = t_route[i4];

                    // Tentatively apply the flip to our isolated route copy
                    flip(t_route, t_pos, t_pos[curr_tail], best_i3);

                    // If this depth yielded the best valid tour so far, save it
                    if (best_step_gain > 1e-7 && best_step_gain > best_path_gain) {
                        best_path_gain = best_step_gain;
                        best_route_state = t_route;
                        foundForI = true;
                    }

                    // Setup variables to plunge to the next depth (k+1)
                    gain_so_far = gain_so_far - dist(curr_tail, best_t3) + dist(best_t3, t4);
                    curr_tail = t4;
                    advanced = true;
                }

                // If we hit a dead end where no candidates yield positive gain, break depth search
                if (!advanced) break;
            }

            // If our depth search found a valid improvement, commit it to the real route
            if (foundForI) {
                route = best_route_state;
                refreshPos(route);
                bestDist = totalDistance(route);

                improved = true;
                totalImproved = true;
                if (useDLB) dlb[i1] = false; // Wake up this node for future checks

                if (m_updateTimer.elapsed() > 100) {
                    emitRouteUpdated(route, bestDist, false);
                }
            }

            if (useDLB && !foundForI) dlb[i1] = true;
        }
    }

    bestDist = totalDistance(route); // Safety recalibration
    return totalImproved;
}

std::vector<std::vector<int>> SolverWorker::getCandidates(int size) const {
    TRACE_SCOPE;
    int n = static_cast<int>(m_cities.size());
    std::vector<std::vector<int>> cand(n);
    if (n < 2) return cand;

    TRACE_MSG("Building KDTree for candidate selection");
    CDT::KDTree::KDTree<double> tree;
    for (int i = 0; i < n; ++i) tree.insert((CDT::VertInd)i, m_cities);

    int k = std::min(size, n - 1);

    TRACE_MSG(QString("Finding %1 nearest neighbors for %2 cities (Parallel Mode)").arg(k).arg(n));
    #pragma omp parallel for schedule(dynamic, 100)
    for (int i = 0; i < n; i++) {
        if (m_stopRequested) continue;
        auto result = tree.kNearest(CDT::V2d<double>(m_cities[i].x, m_cities[i].y), k, m_cities, i);
        cand[i].reserve(k + 4); 
        for (auto idx : result) cand[i].push_back((int)idx);
    }

    if (!m_delaunayEdges.empty()) {
        for (size_t i = 0; i < m_delaunayEdges.size(); i += 2) {
            int u = (int)m_delaunayEdges[i];
            int v = (int)m_delaunayEdges[i + 1];
            if (u >= 0 && u < n && v >= 0 && v < n) {
                cand[u].push_back(v);
                cand[v].push_back(u);
            }
        }
    }

    // Ensure uniqueness and efficient access (Parallel sorting)
    #pragma omp parallel for schedule(dynamic, 200)
    for (int i = 0; i < n; i++) {
        if (m_stopRequested) continue;
        std::sort(cand[i].begin(), cand[i].end());
        cand[i].erase(std::unique(cand[i].begin(), cand[i].end()), cand[i].end());
    }

    return cand;
}


void SolverWorker::runInitialTour() {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 2) { emit finished(0, 0); return; }
        std::vector<int> route = generateInitialTour();
        if (route.empty()) { emit finished(0, 0); return; }
        double d = totalDistance(route);
        emitRouteUpdated(route, d, true);
        emit finished(d, 0);
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runTwoOpt(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        TRACE_MSG("runTwoOpt: Cities count = " + QString::number(n));
        if (n < 2) { emit finished(0,0); return; }

        QElapsedTimer timer; timer.start();

        emit logMessage("Starting solver...");
        std::vector<int> route = initialTour;
        if (route.size() != (size_t)n) {
            TRACE_MSG("Generating initial tour for 2-Opt");
            route = generateInitialTour();
        }
        if (route.empty()) { emit finished(0,0); return; }
        double bestDist = totalDistance(route);
        emitRouteUpdated(route, bestDist, true);

        TRACE_MSG("Invoking multi-stage 2-Opt refinement");
        emit logMessage("2-Opt Stage 1: Candidate search...");
        optimizeTwoOpt(route, bestDist, true, true);
        if (m_stopRequested) { emit finished(bestDist, (int)timer.elapsed()); return; }

        emit logMessage("2-Opt Stage 2: DLB Exhaustive...");
        optimizeTwoOpt(route, bestDist, false, true);
        if (m_stopRequested) { emit finished(bestDist, (int)timer.elapsed()); return; }

        emit logMessage("2-Opt Stage 3: Full Classical cleanup...");
        optimizeTwoOpt(route, bestDist, false, false);
        if (m_stopRequested) {
            TRACE_MSG("2-Opt execution interrupted");
            emit finished(bestDist, (int)timer.elapsed()); return;
        }
        emitRouteUpdated(route, bestDist, true);

        TRACE_MSG("2-Opt execution complete");
        emit finished(bestDist, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runThreeOpt(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 4) { runTwoOpt(initialTour); return; }
        QElapsedTimer timer; timer.start();

        std::vector<int> route = initialTour;
        if (route.size() != (size_t)n) route = generateInitialTour();
        if (route.empty()) { emit finished(0,0); return; }
        double bestDist = totalDistance(route);
        emitRouteUpdated(route, bestDist, true);

        // Triple-stage 3-Opt pipeline
        emit logMessage("3-Opt Stage 1: Candidate search...");
        optimizeThreeOpt(route, bestDist, true, true);
        if (m_stopRequested) { emit finished(bestDist, (int)timer.elapsed()); return; }

        emit logMessage("3-Opt Stage 2: DLB Exhaustive...");
        optimizeThreeOpt(route, bestDist, false, true);
        if (m_stopRequested) { emit finished(bestDist, (int)timer.elapsed()); return; }

        emit logMessage("3-Opt Stage 3: Full Classical pass...");
        optimizeThreeOpt(route, bestDist, false, false);

        emitRouteUpdated(route, bestDist, true);
        emit finished(bestDist, (int)timer.elapsed());
    } catch (...) { emit finished(0, 0); }
}

bool SolverWorker::optimizeThreeOpt(std::vector<int>& route, double& bestDist, bool useCandidates, bool useDLB) {
    int n = static_cast<int>(route.size());
    if (n < 4) return false;

    int totalN = static_cast<int>(m_cities.size());
    std::vector<int> pos(totalN, -1);
    auto refreshPos = [&]() { for (int i = 0; i < n; i++) pos[route[i]] = i; };
    refreshPos();

    if (useCandidates && m_candCache.empty()) {
        int candSize = m_params.value("global_cand_size", 500).toInt();
        m_candCache = getCandidates(candSize);
    }

    std::vector<bool> dlb(n, false);
    bool totalImproved = false;
    bool improved = true;

    auto reverseRange = [&](int s, int e) {
        int rlen = (e - s + n) % n + 1;
        int half = rlen / 2;
        for (int i = 0; i < half; ++i) {
            int i1 = (s + i) % n, i2 = (e - i + n) % n;
            std::swap(route[i1], route[i2]);
            pos[route[i1]] = i1; pos[route[i2]] = i2;
        }
    };

    while (improved && !m_stopRequested) {
        improved = false;
        for (int i = 0; i < n && !m_stopRequested; i++) {
            if (useDLB && dlb[i]) continue;
            bool foundForI = false;
            int A = route[i], B = route[(i + 1) % n];

            if (useCandidates) {
                for (int u2 : m_candCache[A]) {
                    int p2 = pos[u2];
                    if (p2 == i || p2 == (i + 1) % n) continue;
                    // d_CD removed (unused)

                    for (int u3 : m_candCache[B]) {
                        int p3 = pos[u3];
                        if (p3 == i || p3 == (i + 1) % n || p3 == p2 || p3 == (p2 + 1) % n) continue;

                        int idxs[3] = { i, p2, p3 }; std::sort(idxs, idxs + 3);
                        int pA = idxs[0], pB = idxs[1], pC = idxs[2];
                        int v1 = route[pA], v2 = route[(pA+1)%n], v3 = route[pB], v4 = route[(pB+1)%n], v5 = route[pC], v6 = route[(pC+1)%n];

                        double cur_d = dist(v1,v2) + dist(v3,v4) + dist(v5,v6), bestMoveDist = cur_d;
                        double d_13=dist(v1,v3), d_24=dist(v2,v4), d_35=dist(v3,v5), d_46=dist(v4,v6), d_14=dist(v1,v4), d_52=dist(v5,v2), d_36=dist(v3,v6), d_53=dist(v5,v3), d_26=dist(v2,v6), d_15=dist(v1,v5), d_42=dist(v4,v2);
                        int move = -1;
                        if (d_13+d_24+dist(v5,v6) < bestMoveDist-1e-9) { bestMoveDist=d_13+d_24+dist(v5,v6); move=1; }
                        if (dist(v1,v2)+d_35+d_46 < bestMoveDist-1e-9) { bestMoveDist=dist(v1,v2)+d_35+d_46; move=2; }
                        if (d_15+dist(v3,v4)+d_26 < bestMoveDist-1e-9) { bestMoveDist=d_15+dist(v3,v4)+d_26; move=3; }
                        if (d_14+d_52+d_36 < bestMoveDist-1e-9) { bestMoveDist=d_14+d_52+d_36; move=4; }
                        if (d_14+d_53+d_26 < bestMoveDist-1e-9) { bestMoveDist=d_14+d_53+d_26; move=5; }
                        if (d_15+d_42+d_36 < bestMoveDist-1e-9) { bestMoveDist=d_15+d_42+d_36; move=6; }
                        if (d_13+d_52+d_46 < bestMoveDist-1e-9) { bestMoveDist=d_13+d_52+d_46; move=7; }

                        if (move != -1) {
                            double gain = cur_d - bestMoveDist;
                            switch(move) {
                                case 1: reverseRange(pA+1, pB); break;
                                case 2: reverseRange(pB+1, pC); break;
                                case 3: reverseRange(pA+1, pC); break;
                                case 4: reverseRange(pA+1, pB); reverseRange(pB+1, pC); reverseRange(pA+1, pC); break;
                                case 5: reverseRange(pB+1, pC); reverseRange(pA+1, pC); break;
                                case 6: reverseRange(pA+1, pB); reverseRange(pA+1, pC); break;
                                case 7: reverseRange(pA+1, pB); reverseRange(pB+1, pC); break;
                            }
                            bestDist -= gain; improved = true; totalImproved = true;
                            if (useDLB) { dlb[pA] = dlb[pB] = dlb[pC] = false; }
                            emitRouteUpdated(route, bestDist, false); QThread::yieldCurrentThread(); break;
                        }
                    }
                    if (foundForI) break;
                }
            } else {
                double d_AB = dist(A, B);
                for (int j = i + 2; j < n - 3 && !foundForI && !m_stopRequested; j++) {
                    int pB = j, vC = route[pB], vD = route[(pB + 1) % n];
                    double d_CD = dist(vC, vD);
                    for (int k = j + 2; k < (i == 0 ? n - 1 : n) && !foundForI && !m_stopRequested; k++) {
                        int pA = i, pC = k, vE = route[pC], vF = route[(pC + 1) % n];
                        double cur_d = d_AB + d_CD + dist(vE, vF), bestMoveDist = cur_d;
                        double d_AC=dist(A,vC), d_BD=dist(B,vD), d_CE=dist(vC,vE), d_DF=dist(vD,vF), d_AD=dist(A,vD), d_EB=dist(vE,B), d_CF=dist(vC,vF), d_EC=dist(vE,vC), d_BF=dist(B,vF), d_AE=dist(A,vE), d_DB=dist(vD,B);
                        int move = -1;
                        if (d_AC+d_BD+dist(vE,vF) < bestMoveDist-1e-9) { bestMoveDist=d_AC+d_BD+dist(vE,vF); move=1; }
                        if (d_AB+d_CE+d_DF < bestMoveDist-1e-9) { bestMoveDist=d_AB+d_CE+d_DF; move=2; }
                        if (d_AE+d_CD+d_BF < bestMoveDist-1e-9) { bestMoveDist=d_AE+d_CD+d_BF; move=3; }
                        if (d_AD+d_EB+d_CF < bestMoveDist-1e-9) { bestMoveDist=d_AD+d_EB+d_CF; move=4; }
                        if (d_AD+d_EC+d_BF < bestMoveDist-1e-9) { bestMoveDist=d_AD+d_EC+d_BF; move=5; }
                        if (d_AE+d_DB+d_CF < bestMoveDist-1e-9) { bestMoveDist=d_AE+d_DB+d_CF; move=6; }
                        if (d_AC+d_EB+d_DF < bestMoveDist-1e-9) { bestMoveDist=d_AC+d_EB+d_DF; move=7; }
                        if (move != -1) {
                            double gain = cur_d - bestMoveDist;
                            switch(move) {
                                case 1: reverseRange(pA+1, pB); break;
                                case 2: reverseRange(pB+1, pC); break;
                                case 3: reverseRange(pA+1, pC); break;
                                case 4: reverseRange(pA+1, pB); reverseRange(pB+1, pC); reverseRange(pA+1, pC); break;
                                case 5: reverseRange(pB+1, pC); reverseRange(pA+1, pC); break;
                                case 6: reverseRange(pA+1, pB); reverseRange(pA+1, pC); break;
                                case 7: reverseRange(pA+1, pB); reverseRange(pB+1, pC); break;
                            }
                            bestDist -= gain; improved = true; totalImproved = true;
                            if (useDLB) { dlb[pA] = dlb[pB] = dlb[pC] = false; }
                            emitRouteUpdated(route, bestDist, false); QThread::yieldCurrentThread(); break;
                        }
                    }
                }
            }
            if (useDLB && !foundForI) dlb[i] = true;
        }
    }
    bestDist = totalDistance(route); // Safety recalibration
    return totalImproved;
}

bool SolverWorker::optimizeFiveOpt(std::vector<int>& route, double& bestDist, bool useCandidates, bool useDLB) {
    (void)useCandidates; // Acknowledge unused parameter
    int n = static_cast<int>(route.size());
    if (n < 10) return false;

    int totalN = static_cast<int>(m_cities.size());
    std::vector<int> pos(totalN, -1);
    auto refreshPos = [&]() { for (int i = 0; i < n; i++) pos[route[i]] = i; };
    refreshPos();

    std::vector<bool> dlb(n, false);
    bool totalImproved = false;
    bool improved = true;

    while (improved && !m_stopRequested) {
        improved = false;
        for (int i = 0; i < n && !m_stopRequested; i++) {
            if (useDLB && dlb[i]) continue;
            bool foundForI = false;

            int cityA = route[i];
            for (int cityB : m_candCache[cityA]) {
                int pj = pos[cityB]; if (pj == i || pj == (i+1)%n || pj == (i-1+n)%n) continue;
                for (int cityC : m_candCache[cityB]) {
                    int pk = pos[cityC]; if (pk == i || pk == pj) continue;
                    for (int cityD : m_candCache[cityC]) {
                        int pl = pos[cityD]; if (pl == i || pl == pj || pl == pk) continue;
                        for (int cityE : m_candCache[cityA]) { // Loop back slightly
                            int pm = pos[cityE]; if (pm == i || pm == pj || pm == pk || pm == pl) continue;

                            int idxs[5] = { i, pj, pk, pl, pm }; std::sort(idxs, idxs + 5);
                            int t1 = idxs[0], t2 = idxs[1], t3 = idxs[2], t4 = idxs[3], t5 = idxs[4];
                            if (t2-t1 < 2 || t3-t2 < 2 || t4-t3 < 2 || t5-t4 < 2 || (n-1-t5+t1) < 1) continue;

                            int n1 = route[t1], n2 = route[(t1+1)%n], n3 = route[t2], n4 = route[(t2+1)%n];
                            int n5 = route[t3], n6 = route[(t3+1)%n], n7 = route[t4], n8 = route[(t4+1)%n];
                            int n9 = route[t5], n10 = route[(t5+1)%n];

                            double cur_d = dist(n1,n2) + dist(n3,n4) + dist(n5,n6) + dist(n7,n8) + dist(n9,n10);
                            // S1,S4,S2,S5,S3: (t1,t3+1), (t4,t1+1), (t2,t4+1), (t5,t2+1), (t3,t5+1)
                            double new_d = dist(n1,n6) + dist(n7,n2) + dist(n3,n8) + dist(n9,n4) + dist(n5,n10);

                            if (new_d < cur_d - 1e-9) {
                                std::vector<int> nr; nr.reserve(n);
                                for(int k=0; k<=t1; k++) nr.push_back(route[k]);
                                for(int k=t3+1; k<=t4; k++) nr.push_back(route[k]);
                                for(int k=t1+1; k<=t2; k++) nr.push_back(route[k]);
                                for(int k=t4+1; k<=t5; k++) nr.push_back(route[k]);
                                for(int k=t2+1; k<=t3; k++) nr.push_back(route[k]);
                                for(int k=t5+1; k<n; k++) nr.push_back(route[k]);

                                bestDist -= (cur_d - new_d);
                                route = std::move(nr); refreshPos();
                                improved = true; totalImproved = true; foundForI = true;
                                if (useDLB) { dlb[t1] = dlb[t2] = dlb[t3] = dlb[t4] = dlb[t5] = false; }
                                emitRouteUpdated(route, bestDist, false); break;
                            }
                        }
                        if (foundForI) break;
                    }
                    if (foundForI) break;
                }
                if (foundForI) break;
            }
            if (useDLB && !foundForI) dlb[i] = true;
        }
    }
    return totalImproved;
}

void SolverWorker::runFiveOpt(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 5) { emit finished(0,0); return; }
        QElapsedTimer timer; timer.start();
        emit logMessage("Starting 5-Opt (Pent-Bridge) Local Search...");
        
        std::vector<int> tour = initialTour;
        if (tour.size() != (size_t)n) tour = generateInitialTour();
        if (tour.empty()) { emit finished(0,0); return; }
        
        double currentDist = totalDistance(tour);
        emitRouteUpdated(tour, currentDist, true);

        if (m_stopRequested) { emit finished(currentDist, (int)timer.elapsed()); return; }
        
        bool improved = true;
        while (improved && !m_stopRequested) {
            improved = optimizeFiveOpt(tour, currentDist, true, true);
            if (improved) {
                optimizeTwoOpt(tour, currentDist, true, true);
                optimizeThreeOpt(tour, currentDist, true, true);
            }
        }
        
        emitRouteUpdated(tour, currentDist, true);
        emit logMessage("5-Opt Finished.");
        emit finished(currentDist, (int)timer.elapsed());
    } catch (...) {
        emit logMessage("5-Opt Error occurred.");
        emit finished(0, 0);
    }
}


void SolverWorker::runIteratedLocalSearch(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 8) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Iterated Local Search (ILS)...");
        std::vector<int> best = initialTour;
        if (best.size() != (size_t)n) {
            TRACE_MSG("Generating initial tour for ILS");
            best = generateInitialTour();
        }
        if (best.empty()) { emit finished(0,0); return; }
        double bestDist = totalDistance(best);
        int steps = m_params.value("ils_steps", 500).toInt();
        double kickFract = m_params.value("ils_kick", 0.25).toDouble();
        performILS(best, bestDist, steps, kickFract);
        emitRouteUpdated(best, bestDist, true);
        emit finished(bestDist, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

bool SolverWorker::performILS(std::vector<int>& best, double& bestDist, int steps, double kickFract) {
    int n = static_cast<int>(best.size());
    if (n < 4) return false;
    bool totalImproved = false;
    
    // Initial optimization
    optimizeTwoOpt(best, bestDist, true, true);
    
    QString searchChoice = m_params.value("ils_search_type", "2-Opt Local Search").toString();

    for (int step = 0; step < steps && !m_stopRequested; ++step) {
        std::vector<int> next = best;
        double nextDist = bestDist;

        // Kick Strategy: Large Random Reversal
        int s = std::uniform_int_distribution<int>(0, n-2)(m_rng);
        int maxLen = std::max(2, static_cast<int>(n * kickFract));
        int len = std::uniform_int_distribution<int>(2, maxLen)(m_rng);
        int e = std::min(n-1, s + len);
        std::reverse(next.begin() + s, next.begin() + e + 1);
        nextDist = totalDistance(next);

        if (searchChoice == "3-Opt Local Search") optimizeThreeOpt(next, nextDist, true, true);
        else if (searchChoice == "5-Opt Local Search") optimizeFiveOpt(next, nextDist, true, true);
        else optimizeTwoOpt(next, nextDist, true, true);
        
        if (nextDist < bestDist - 1e-9) {
            bestDist = nextDist; best = next;
            totalImproved = true;
            if (m_updateTimer.elapsed() > 50) emitRouteUpdated(best, bestDist);
        }
    }
    return totalImproved;
}

void SolverWorker::runCuckooSearch(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }
        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Cuckoo Search...");
        int nests = m_params.value("cs_nests", 40).toInt();
        double pa = m_params.value("cs_prob", 0.25).toDouble();

        TRACE_MSG("Initializing Cuckoo population");
        std::vector<std::vector<int>> pop(nests);
        std::vector<double> fitness(nests);
        pop.at(0) = initialTour;
        if (pop.at(0).size() != (size_t)n) pop.at(0) = generateInitialTour();

        if (pop.at(0).empty()) { emit finished(0,0); return; }
        fitness.at(0) = totalDistance(pop.at(0)); optimizeTwoOpt(pop.at(0), fitness.at(0), true, true);
        if (m_stopRequested) { emit finished(fitness.at(0), (int)timer.elapsed()); return; }

        std::uniform_int_distribution<int> dCity(0, n - 1);
        for (int i = 1; i < nests; i++) {
            pop.at(i) = pop.at(0);
            // Light perturbation to maintain initial tour quality while adding diversity
            int numSwaps = std::max(2, (int)std::log10(n) * 2);
            for(int s=0; s<numSwaps; s++) {
                int i1 = dCity(m_rng), i2 = dCity(m_rng);
                std::swap(pop.at(i).at(i1), pop.at(i).at(i2));
            }
            fitness.at(i) = totalDistance(pop.at(i));
            if (m_stopRequested) { emit finished(fitness.at(0), (int)timer.elapsed()); return; }
        }

        double bestDist = fitness.at(0); std::vector<int> bestRoute = pop.at(0);
        emitRouteUpdated(bestRoute, bestDist, true);
        std::uniform_int_distribution<int> dNest(0, nests - 1), dJump(1, 4);
        std::uniform_real_distribution<double> dReal(0.0, 1.0);

        QString variant = m_params.value("cs_variant", "Standard Memetic (S-CS)").toString();
        emit logMessage("Selected Variant: " + variant);

        int maxIters = 2000;
        TRACE_MSG("Entering Multi-Variant Cuckoo main loop");
        for (int iter = 0; iter < maxIters && !m_stopRequested; iter++) {
            int i = dNest(m_rng); std::vector<int> next = pop.at(i);

            int jumps = 1;
            if (variant == "Adaptive Step-Size (A-CS)") {
                int maxJumps = std::max(1, 15 - (int)(15.0 * iter / maxIters));
                jumps = std::uniform_int_distribution<int>(1, maxJumps)(m_rng);
            } else if (variant == "Chaotic Fractal Flights (CF-CS)") {
                static double chaos = 0.6180339887; // Golden ratio start
                chaos = 4.0 * chaos * (1.0 - chaos); // Logistic map r=4.0
                jumps = (chaos < 0.6) ? 1 : (chaos < 0.9 ? 2 : (chaos < 0.98 ? 4 : 8));
            } else {
                double r = dReal(m_rng);
                jumps = (r < 0.6) ? 1 : (r < 0.9 ? 2 : (r < 0.98 ? 4 : 8));
            }

            for(int j=0; j<jumps && !m_stopRequested; j++) {
                int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                std::reverse(next.begin() + s1, next.begin() + s2 + 1);
            }
            double dNext = totalDistance(next);

            if (dNext < bestDist * 1.1) {
                optimizeTwoOpt(next, dNext, true, true);
            }

            int randomNest;
            if (variant == "Cellular Cuckoo (C-CS)") {
                randomNest = (i + ((dReal(m_rng) < 0.5) ? 1 : (nests - 1))) % nests;
            } else {
                randomNest = dNest(m_rng);
            }

            if (dNext < fitness.at(randomNest) - 1e-9) {
                pop.at(randomNest) = next;
                fitness.at(randomNest) = dNext;
            }

            if (dNext < bestDist - 1e-9) {
                bestDist = dNext;
                bestRoute = next;
                if (m_updateTimer.elapsed() > 100) emitRouteUpdated(bestRoute, bestDist, false);
            }

            if (variant == "Elitist Replacement (E-CS)") {
                // Elite Replacement Discovery Phase
                if (iter % 10 == 0) {
                    std::vector<int> idx(nests); std::iota(idx.begin(), idx.end(), 0);
                    std::sort(idx.begin(), idx.end(), [&](int a, int b){ return fitness[a] < fitness[b]; });
                    int replaced = (int)(nests * pa);
                    for (int j = nests - replaced; j < nests && !m_stopRequested; j++) {
                        int eliteIdx = idx[std::min((int)(dReal(m_rng) * 3), nests - 1)];
                        std::vector<int> mutant = pop[eliteIdx];
                        int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                        std::reverse(mutant.begin() + s1, mutant.begin() + s2 + 1);
                        pop[idx[j]] = mutant;
                        fitness[idx[j]] = totalDistance(mutant);
                    }
                }
            } else if (variant == "ILS-Guided Discovery (ILS-CS)") {
                // ILS-Guided Discovery: Replaces abandoned nests with short ILS runs on global best
                for (int j = 0; j < nests && !m_stopRequested; j++) {
                    if (dReal(m_rng) < pa) {
                        std::vector<int> candidate = bestRoute;
                        double dCand = bestDist;
                        int kSteps = m_params.value("cs_kick_steps", 50).toInt();
                        double kSize = m_params.value("cs_kick_size", 0.15).toDouble();
                        performILS(candidate, dCand, kSteps, kSize); 
                        if (dCand < fitness.at(j)) {
                            pop.at(j) = candidate;
                            fitness.at(j) = dCand;
                        }
                    }
                }
            } else {
                // Standard Pure Random Discovery Phase
                for (int j = 0; j < nests && !m_stopRequested; j++) {
                    if (dReal(m_rng) < pa) {
                        std::vector<int> randomNestRoute = bestRoute;
                        std::shuffle(randomNestRoute.begin() + 1, randomNestRoute.end() - 1, m_rng);
                        double dRandom = totalDistance(randomNestRoute);

                        if (dRandom < fitness.at(j)) {
                            pop.at(j) = randomNestRoute;
                            fitness.at(j) = dRandom;
                        }
                    }
                    if (fitness.at(j) < bestDist - 1e-9) {
                        bestDist = fitness.at(j);
                        bestRoute = pop.at(j);
                        if (m_updateTimer.elapsed() > 100) emitRouteUpdated(bestRoute, bestDist, false);
                    }
                }
            }

            if (iter % 100 == 0) {
                emit logMessage(QString("CS Iter %1 | Best: %2").arg(iter).arg(bestDist, 0, 'f', 2));
                QThread::yieldCurrentThread();
            }
        }
        TRACE_MSG("Cuckoo loop complete");
        emitRouteUpdated(bestRoute, bestDist, true);
        emit finished(bestDist, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}
void SolverWorker::runGeneticAlgorithm(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Starting High-Performance Memetic GA...");

        const int POP = m_params.value("ga_pop", 50).toInt();
        const int GENS = m_params.value("ga_gen", 2000).toInt();
        const int BASE_MUT = m_params.value("ga_mut", 15).toInt();

        TRACE_MSG("Initializing GA population");
        std::vector<std::vector<int>> pop(POP);
        std::vector<double> fitness(POP);

        pop.at(0) = initialTour;
        if (pop.at(0).size() != (size_t)n) pop.at(0) = generateInitialTour();
        if (pop.at(0).empty()) { emit finished(0,0); return; }

        fitness.at(0) = totalDistance(pop.at(0));
        optimizeTwoOpt(pop.at(0), fitness.at(0), true, true);
        if (m_stopRequested) { emit finished(fitness.at(0), (int)timer.elapsed()); return; }
        emitRouteUpdated(pop.at(0), fitness.at(0), true); // Initial baseline

        std::uniform_int_distribution<int> dCity(0, n - 1);
        for (int i = 1; i < POP; i++) {
            pop.at(i) = pop.at(0);
            // Only shuffle one or two individuals, keep the rest as high-quality perturbations
            if (i > (POP - 3)) {
                std::shuffle(pop.at(i).begin() + 1, pop.at(i).end() - 1, m_rng);
            } else {
                int numSwaps = std::max(2, (int)std::log10(n) * 2);
                for(int s=0; s<numSwaps; s++) {
                    int i1 = dCity(m_rng), i2 = dCity(m_rng);
                    std::swap(pop.at(i).at(i1), pop.at(i).at(i2));
                }
            }
            fitness.at(i) = totalDistance(pop.at(i));
            if (i % 5 == 0) optimizeTwoOpt(pop.at(i), fitness.at(i), true, true);
            if (m_stopRequested) { emit finished(fitness.at(0), (int)timer.elapsed()); return; }
        }

        double bestGlobal = 1e18;
        std::vector<int> bestRoute;
        int noImprove = 0;
        std::uniform_int_distribution<int> dPop(0, POP - 1), d100(0, 99);

        TRACE_MSG("Entering GA generation loop");
        QString variant = m_params.value("ga_variant", "Standard Memetic (S-GA)").toString();
        emit logMessage("Selected Variant: " + variant);

        int numIslands = 5;
        int islandSize = POP / numIslands;
        std::vector<int> ages(POP, 0);

        auto selectTournament = [&](int start, int end, int k) {
            int best = -1;
            for (int i = 0; i < k; ++i) {
                int cand = std::uniform_int_distribution<int>(start, end)(m_rng);
                if (best == -1 || fitness.at(cand) < fitness.at(best)) best = cand;
            }
            return best;
        };

        auto eaxCrossover = [&](const std::vector<int>& p1, const std::vector<int>& p2) {
            std::vector<std::vector<int>> adjA(n), adjB(n);
            for (int i=0; i<n; i++) {
                adjA[p1[i]].push_back(p1[(i+1)%n]);
                adjA[p1[i]].push_back(p1[(i+n-1)%n]);
                adjB[p2[i]].push_back(p2[(i+1)%n]);
                adjB[p2[i]].push_back(p2[(i+n-1)%n]);
            }
            std::vector<std::vector<int>> uniqueA(n), uniqueB(n);
            for (int i=0; i<n; i++) {
                for (int v : adjA[i]) {
                    if (std::find(adjB[i].begin(), adjB[i].end(), v) == adjB[i].end()) uniqueA[i].push_back(v);
                }
                for (int v : adjB[i]) {
                    if (std::find(adjA[i].begin(), adjA[i].end(), v) == adjA[i].end()) uniqueB[i].push_back(v);
                }
            }
            struct Cycle { std::vector<std::pair<int,int>> edgesA, edgesB; };
            std::vector<Cycle> cycles;
            std::vector<std::unordered_set<int>> usedA(n), usedB(n);
            for (int start = 0; start < n; ++start) {
                for (int nxt : uniqueA[start]) {
                    if (usedA[start].count(nxt)) continue;
                    Cycle c; bool inA = true; int node = start; int target = nxt;
                    int sanity = 0;
                    while (sanity++ < n * 2) {
                        if (inA) { c.edgesA.push_back({node, target}); usedA[node].insert(target); usedA[target].insert(node); }
                        else { c.edgesB.push_back({node, target}); usedB[node].insert(target); usedB[target].insert(node); }
                        node = target; inA = !inA; int nextTarget = -1;
                        if (inA) { for(int v: uniqueA[node]) if(!usedA[node].count(v)) { nextTarget = v; break; } }
                        else { for(int v: uniqueB[node]) if(!usedB[node].count(v)) { nextTarget = v; break; } }
                        if (nextTarget == -1) break;
                        target = nextTarget;
                    }
                    if (!c.edgesA.empty()) cycles.push_back(c);
                }
            }
            std::vector<std::vector<int>> intGraph(n);
            for (int i=0; i<n; i++) intGraph[i] = adjA[i];
            for (size_t i=0; i<cycles.size(); i++) {
                if (d100(m_rng) < 50) {
                    for (auto& edge : cycles[i].edgesA) {
                        auto it1 = std::find(intGraph[edge.first].begin(), intGraph[edge.first].end(), edge.second);
                        if(it1 != intGraph[edge.first].end()) intGraph[edge.first].erase(it1);
                        auto it2 = std::find(intGraph[edge.second].begin(), intGraph[edge.second].end(), edge.first);
                        if(it2 != intGraph[edge.second].end()) intGraph[edge.second].erase(it2);
                    }
                    for (auto& edge : cycles[i].edgesB) {
                        intGraph[edge.first].push_back(edge.second);
                        intGraph[edge.second].push_back(edge.first);
                    }
                }
            }
            std::vector<int> tourIdx(n, -1);
            std::vector<std::vector<int>> subTours;
            for (int i=0; i<n; i++) {
                if (tourIdx[i] != -1) continue;
                std::vector<int> tour; int curr = i; int prev = -1;
                int sanity = 0;
                while (sanity++ < n + 2) {
                    tour.push_back(curr); tourIdx[curr] = (int)subTours.size();
                    int next = -1;
                    for (int pt : intGraph[curr]) if (pt != prev) { next = pt; break; }
                    if (next == -1 || next == i) break;
                    prev = curr; curr = next;
                }
                subTours.push_back(tour);
            }
            while (subTours.size() > 1) {
                int t1 = 0;
                for(size_t i=1; i<subTours.size(); i++) if(subTours[i].size() > subTours[t1].size()) t1 = (int)i;
                int t2 = (t1 == 0) ? 1 : 0;
                int bestV1=-1, bestU2=-1, bestV2=-1;
                double bestCost = 1e18;
                for (size_t i=0; i<subTours[t1].size(); i++) {
                    int u1 = subTours[t1][i], v1 = subTours[t1][(i+1)%subTours[t1].size()];
                    for (size_t j=0; j<subTours[t2].size(); j++) {
                        int u2 = subTours[t2][j], v2 = subTours[t2][(j+1)%subTours[t2].size()];
                        double cost1 = dist(u1, u2) + dist(v1, v2) - (dist(u1, v1) + dist(u2, v2));
                        double cost2 = dist(u1, v2) + dist(v1, u2) - (dist(u1, v1) + dist(u2, v2));
                        if (cost1 < bestCost) { bestCost=cost1; bestV1=v1; bestU2=u2; bestV2=v2; }
                        if (cost2 < bestCost) { bestCost=cost2; bestV1=v1; bestU2=v2; bestV2=u2; }
                    }
                }
                std::vector<int> mT1 = subTours[t1];
                std::vector<int> mT2 = subTours[t2];
                auto it1 = std::find(mT1.begin(), mT1.end(), bestV1); if(it1!=mT1.end()) std::rotate(mT1.begin(), it1, mT1.end());
                auto it2 = std::find(mT2.begin(), mT2.end(), bestU2); if(it2!=mT2.end()) std::rotate(mT2.begin(), it2, mT2.end());
                if (!mT2.empty() && mT2.back() != bestV2) std::reverse(mT2.begin() + 1, mT2.end());
                std::vector<int> merged; merged.reserve(mT1.size() + mT2.size());
                merged.insert(merged.end(), mT1.begin(), mT1.end()); merged.insert(merged.end(), mT2.begin(), mT2.end());
                subTours[t1] = merged; subTours.erase(subTours.begin() + t2);
            }
            if (subTours.empty() || subTours[0].size() != (size_t)n) return crossoverOX(p1, p2);
            return subTours[0];
        };

        TRACE_MSG("Entering Multi-Variant GA generation loop");
        for (int gen = 0; gen < GENS && !m_stopRequested; gen++) {
            int bestInGenIdx = 0;
            for (int i = 1; i < POP; i++) {
                ages[i]++; // ALPS Aging
                if (fitness.at(i) < fitness.at(bestInGenIdx)) bestInGenIdx = i;
            }

            if (fitness.at(bestInGenIdx) < bestGlobal - 1e-9) {
                bestGlobal = fitness.at(bestInGenIdx);
                bestRoute = pop.at(bestInGenIdx);
                if (m_updateTimer.elapsed() > 100) emitRouteUpdated(bestRoute, bestGlobal, false);
                noImprove = 0;
            } else {
                noImprove++;
            }

            // Variant 2: Island Model Migration
            if (variant == "Island Model Distributed (IM-GA)" && gen % 50 == 0) {
                for (int isl = 0; isl < numIslands; ++isl) {
                    int bestInIsland = isl * islandSize;
                    for(int i = 1; i < islandSize; ++i) {
                        int idx = isl * islandSize + i;
                        if (fitness.at(idx) < fitness.at(bestInIsland)) bestInIsland = idx;
                    }
                    int targetIsland = (isl + dPop(m_rng) % (numIslands - 1) + 1) % numIslands;
                    int targetReplace = targetIsland * islandSize + dPop(m_rng) % islandSize;
                    pop.at(targetReplace) = pop.at(bestInIsland);
                    fitness.at(targetReplace) = fitness.at(bestInIsland);
                }
            }

            // Main Reproduction Loop
            for (int k = 0; k < 2 && !m_stopRequested; k++) {
                int p1idx = -1, p2idx = -1;

                if (variant == "Cellular Grid (CG-GA)") {
                    p1idx = dPop(m_rng);
                    int offset = (d100(m_rng) % 3) - 1; // -1, 0, 1
                    p2idx = (p1idx + offset + POP) % POP;
                } else if (variant == "Age-Layered Population (ALPS)") {
                    p1idx = selectTournament(0, POP - 1, 3);
                    p2idx = selectTournament(0, POP - 1, 3);
                    int attempts = 0;
                    while (std::abs(ages[p1idx] - ages[p2idx]) > 20 && attempts++ < 10) {
                        p2idx = selectTournament(0, POP - 1, 3);
                    }
                } else if (variant == "Island Model Distributed (IM-GA)") {
                    int isl = dPop(m_rng) % numIslands;
                    p1idx = selectTournament(isl * islandSize, isl * islandSize + islandSize - 1, 3);
                    p2idx = selectTournament(isl * islandSize, isl * islandSize + islandSize - 1, 3);
                } else {
                    p1idx = selectTournament(0, POP - 1, 3);
                    p2idx = selectTournament(0, POP - 1, 3);
                }

                // Crossover Operator
                std::vector<int> child;
                bool useEAX = (variant == "Edge Assembly Crossover (EAX)");

                if (useEAX) {
                    child = eaxCrossover(pop.at(p1idx), pop.at(p2idx));
                } else {
                    child = crossoverOX(pop.at(p1idx), pop.at(p2idx));
                }

                // Mutation
                int mutRate = (noImprove > 50) ? BASE_MUT * 2 : BASE_MUT;

                if (d100(m_rng) < mutRate) {
                    int m1 = dCity(m_rng), m2 = dCity(m_rng); if (m1 > m2) std::swap(m1, m2);
                    std::reverse(child.begin() + m1, child.begin() + m2 + 1);
                }

                double cFit = totalDistance(child);
                optimizeTwoOpt(child, cFit, true, true);

                // Replacement Strategy
                if (variant == "Edge Assembly Crossover (EAX)") {
                    // Steady-State Absolute Replacement
                    int worstIdx = 0;
                    for (int i = 1; i < POP; i++) if (fitness.at(i) > fitness.at(worstIdx)) worstIdx = i;
                    pop.at(worstIdx) = std::move(child);
                    fitness.at(worstIdx) = cFit;
                    ages[worstIdx] = 0;
                } else {
                    // Standard Weakest Replacement via Tournament
                    int cand1 = dPop(m_rng), cand2 = dPop(m_rng);
                    int worstIdx = (fitness.at(cand1) > fitness.at(cand2)) ? cand1 : cand2;
                    if (cFit < fitness.at(worstIdx) || d100(m_rng) < 5) {
                        pop.at(worstIdx) = std::move(child);
                        fitness.at(worstIdx) = cFit;
                        ages[worstIdx] = 0;
                    }
                }
            }

            if (gen % 100 == 0) {
                emit logMessage(QString("GA Iter %1 | Best: %2").arg(gen).arg(bestGlobal, 0, 'f', 2));
                QThread::yieldCurrentThread();
            }

            if (noImprove > 200) {
                TRACE_MSG("GA stagnant - triggering hyper-mutation");
                for (int i = POP / 2; i < POP; i++) {
                    std::shuffle(pop.at(i).begin() + 1, pop.at(i).end() - 1, m_rng);
                    fitness.at(i) = totalDistance(pop.at(i));
                    optimizeTwoOpt(pop.at(i), fitness.at(i), true, true);
                    ages[i] = 0;
                }
                noImprove = 0;
            }
        }
        TRACE_MSG("GA execution complete");
        emitRouteUpdated(bestRoute, bestGlobal, true);
        emit finished(bestGlobal, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runGrayWolfOptimization(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }
        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Gray Wolf Optimization...");
        int size = m_params.value("gwo_size", 30).toInt();
        int maxIters = m_params.value("gwo_iters", 500).toInt();

        TRACE_MSG("Initializing GWO pack");
        std::vector<std::vector<int>> pack(size); std::vector<double> lengths(size);
        pack.at(0) = initialTour;
        if (pack.at(0).size() != (size_t)n) pack.at(0) = generateInitialTour();

        if (pack.at(0).empty()) { emit finished(0,0); return; }
        lengths.at(0) = totalDistance(pack.at(0)); optimizeTwoOpt(pack.at(0), lengths.at(0), true, true);
        if (m_stopRequested) { emit finished(lengths.at(0), (int)timer.elapsed()); return; }

        std::uniform_int_distribution<int> dCity(0, n-1);
        for (int i = 1; i < size; i++) {
            pack.at(i) = pack.at(0);
            int numSwaps = std::max(2, (int)std::log10(n) * 2);
            for(int s=0; s<numSwaps; s++) {
                int i1 = dCity(m_rng), i2 = dCity(m_rng);
                std::swap(pack.at(i).at(i1), pack.at(i).at(i2));
            }
            lengths.at(i) = totalDistance(pack.at(i));
            if (m_stopRequested) { emit finished(lengths.at(0), (int)timer.elapsed()); return; }
        }

        auto sortPack = [&]() {
            std::vector<int> idx(size); std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return lengths.at(a) < lengths.at(b); });
            std::vector<std::vector<int>> np(size); std::vector<double> nl(size);
            for (int i = 0; i < size; i++) { np.at(i) = pack.at(idx.at(i)); nl.at(i) = lengths.at(idx.at(i)); }
            pack = np; lengths = nl;
        };
        sortPack();
        if (m_stopRequested) { emit finished(lengths.at(0), (int)timer.elapsed()); return; }
        emitRouteUpdated(pack.at(0), lengths.at(0), true);

        std::uniform_real_distribution<double> dReal(0.0, 1.0);

        QString variant = m_params.value("gwo_variant", "Standard Memetic (S-GWO)").toString();
        emit logMessage("Selected Variant: " + variant);

        int numLeaders = (variant == "Multi-Leader Swarm (ML-GWO)") ? std::min(5, size) : 3;
        int startIndex = (variant == "Hierarchical Wolf (H-GWO)") ? 1 : numLeaders;

        TRACE_MSG("Entering Multi-Variant GWO hunting loop");
        for (int iter = 0; iter < maxIters && !m_stopRequested; iter++) {

            double a_param = 2.0 * (1.0 - (double)iter / (double)maxIters);
            if (variant == "Non-Linear Parameter (NL-GWO)") {
                a_param = 2.0 * std::cos((M_PI / 2.0) * ((double)iter / (double)maxIters));
            }

            for (int i = startIndex; i < size && !m_stopRequested; i++) {

                if (variant == "Enhanced Exploration (EE-GWO)" && dReal(m_rng) < 0.30 * (a_param / 2.0)) {
                    int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                    std::reverse(pack.at(i).begin() + s1, pack.at(i).begin() + s2 + 1);
                    lengths.at(i) = totalDistance(pack.at(i));
                }
                else if (dReal(m_rng) < 0.1) {
                    int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                    std::reverse(pack.at(i).begin() + s1, pack.at(i).begin() + s2 + 1);
                    lengths.at(i) = totalDistance(pack.at(i));
                } else {
                    int leaderIdx = 0;
                    if (variant == "Hierarchical Wolf (H-GWO)") {
                        if (i == 1) leaderIdx = 0; // Beta follows Alpha
                        else if (i == 2) leaderIdx = 1; // Delta follows Beta
                        else leaderIdx = 2; // Omega follows Delta
                    } else if (variant == "Multi-Leader Swarm (ML-GWO)") {
                        leaderIdx = std::uniform_int_distribution<int>(0, numLeaders - 1)(m_rng);
                    } else {
                        leaderIdx = (dReal(m_rng) < 0.5) ? 0 : (dReal(m_rng) < 0.8 ? 1 : 2);
                    }

                    const auto& leader = pack.at(leaderIdx);
                    int s = dCity(m_rng), e = dCity(m_rng); if (s > e) std::swap(s, e);
                    std::vector<int> child(n, -1); std::vector<bool> inC(n, false);
                    for (int j = s; j <= e; j++) { child.at(j) = leader.at(j); inC.at(leader.at(j)) = true; }
                    int ci = (e+1)%n, pi = (e+1)%n, filled = (e-s+1);
                    while (filled < n && !m_stopRequested) { if (!inC.at(pack.at(i).at(pi))) { child.at(ci) = pack.at(i).at(pi); inC.at(pack.at(i).at(pi)) = true; ci = (ci+1)%n; filled++; } pi = (pi+1)%n; }
                    pack.at(i) = child; lengths.at(i) = totalDistance(child);
                }

                if (lengths.at(i) < lengths.at(0) * 1.05) {
                    if (variant == "Non-Linear Parameter (NL-GWO)" && a_param > 1.5 && dReal(m_rng) < 0.5) {
                        // Skip 2-opt early to vastly expand exploration radius
                    } else {
                        optimizeTwoOpt(pack.at(i), lengths.at(i), true, true);
                    }
                }
            }
            sortPack();
            
            // ILS-Guided Alpha Extension
            if (variant == "ILS-Guided Alpha (ILS-GWO)" && dReal(m_rng) < 0.15) {
                // Take the current Alpha, run a short ILS, and place it back if improved
                std::vector<int> candidate = pack.at(0);
                double dCand = lengths.at(0);
                int kSteps = m_params.value("gwo_kick_steps", 50).toInt();
                double kSize = m_params.value("gwo_kick_size", 0.15).toDouble();
                performILS(candidate, dCand, kSteps, kSize);
                if (dCand < lengths.at(0)) {
                    pack.at(0) = candidate; lengths.at(0) = dCand;
                }
            }
            if (m_updateTimer.elapsed() > 100) emitRouteUpdated(pack.at(0), lengths.at(0), false);
            if (iter % 100 == 0) emit logMessage(QString("GWO Iter %1 | Best: %2").arg(iter).arg(lengths.at(0), 0, 'f', 2));
        }
        TRACE_MSG("GWO loop complete");
        emitRouteUpdated(pack.at(0), lengths.at(0), true);
        emit finished(lengths.at(0), (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runTabuSearch(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }
        QElapsedTimer timer; timer.start();
        emit logMessage("Optimization in progress: Premium Tabu Search...");

        std::vector<int> current = initialTour;
        if (current.size() != (size_t)n) current = generateInitialTour();
        if (current.empty()) { emit finished(0,0); return; }

        double currentDist = totalDistance(current), bestDist = currentDist;
        std::vector<int> best = current;
        emitRouteUpdated(best, bestDist, true);

        int candSize = m_params.value("global_cand_size", 500).toInt();
        auto candList = getCandidates(candSize);

        std::vector<int> pos(n, -1);
        auto refreshPos = [&](){ for(int i=0; i<n; i++) pos[current[i]] = i; };
        refreshPos();

        // Advanced Tabu Tracking
        std::deque<std::pair<int, int>> tabuQueue;
        std::vector<std::unordered_set<int>> tabuFast(n);

        // Substantially increased runtime and dynamic escapes
        int maxSteps = m_params.value("tabu_steps", 5000).toInt();
        int tenure = m_params.value("tabu_tenure", 50).toInt();
        const int TENURE = std::max(10, std::min(tenure, n));
        int stagnant = 0;

        TRACE_MSG("Entering Tabu search loop");
        for (int step = 0; step < maxSteps && !m_stopRequested; ++step) {
            int best_i = -1, best_j = -1;
            double best_gain = -1e18; // Allow negative gains to escape local optima

            // Neighborhood Search
            for (int i = 0; i < n && !m_stopRequested; ++i) {
                int a = current[i], b = current[(i+1)%n];

                for (int c : candList[a]) {
                    int p_c = pos[c];
                    if (p_c == i || p_c == (i+1)%n) continue;

                    int d = current[(p_c+1)%n];
                    double gain = (dist(a,b) + dist(c,d)) - (dist(a,c) + dist(b,d));

                    // Tabu check: we forbid adding the edges (a,c) and (b,d) if they are in the list
                    bool isTabu = tabuFast[a].count(c) || tabuFast[b].count(d);

                    // Aspiration criterion: override Tabu if it beats the all-time GLOBAL best
                    if (isTabu && (currentDist - gain >= bestDist - 1e-9)) {
                        continue;
                    }

                    if (gain > best_gain) {
                        best_gain = gain; best_i = i; best_j = p_c;
                    }
                }
            }

            if (best_i != -1) {
                // Capture the edges being broken BEFORE the array rotates
                int broken_a = current[best_i], broken_b = current[(best_i+1)%n];
                int broken_c = current[best_j], broken_d = current[(best_j+1)%n];

                // Apply 2-opt Swap
                int s = (best_i+1)%n, e = best_j;
                if (s <= e) {
                    std::reverse(current.begin() + s, current.begin() + e + 1);
                } else {
                    std::rotate(current.begin(), current.begin() + s, current.end());
                    std::reverse(current.begin(), current.begin() + ((e - s + n) % n) + 1);
                }

                currentDist -= best_gain;
                refreshPos();

                // Track Tabu Edges (forbid adding the old broken edges back)
                tabuFast[broken_a].insert(broken_b); tabuFast[broken_b].insert(broken_a);
                tabuFast[broken_c].insert(broken_d); tabuFast[broken_d].insert(broken_c);
                tabuQueue.push_back({broken_a, broken_b});
                tabuQueue.push_back({broken_c, broken_d});

                while ((int)tabuQueue.size() > TENURE * 2) {
                    auto pop = tabuQueue.front(); tabuQueue.pop_front();
                    tabuFast[pop.first].erase(pop.second); tabuFast[pop.second].erase(pop.first);
                }

                if (currentDist < bestDist - 1e-9) {
                    bestDist = currentDist; best = current; stagnant = 0;
                    if (m_updateTimer.elapsed() > 100) {
                        emitRouteUpdated(best, bestDist, false);
                    }
                } else {
                    stagnant++;
                }

                // Active Diversification "Kick" if thoroughly trapped
                if (stagnant > 250) {
                    for(int k = 0; k < 4; ++k) {
                        int r1 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                        int r2 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                        if (r1 > r2) std::swap(r1, r2);
                        std::reverse(current.begin() + r1, current.begin() + r2 + 1);
                    }
                    currentDist = totalDistance(current);
                    refreshPos();
                    stagnant = 0;
                    emit logMessage(QString("Tabu: Diversification kick triggered at step %1!").arg(step));
                }

            } else {
                break; // No valid moves mathematically exist
            }

            if (step % 200 == 0) {
                emit logMessage(QString("Tabu Step %1 | Best: %2 | Current: %3").arg(step).arg(bestDist, 0, 'f', 2).arg(currentDist, 0, 'f', 2));
                QThread::yieldCurrentThread();
            }
        }

        emit logMessage(QString("Tabu Complete | Final Best: %1 | Evaluated %2 steps").arg(bestDist, 0, 'f', 2).arg(maxSteps));
        emitRouteUpdated(best, bestDist, true);
        emit finished(bestDist, (int)timer.elapsed());

    } catch (...) {
        emit finished(0, 0);
    }
}


void SolverWorker::runLinKernighan(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        const int n = static_cast<int>(m_cities.size());
        if (n < 4) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Lin-Kernighan (LK): High-performance search initiated...");

        std::vector<int> tour = initialTour;
        if (tour.size() != (size_t)n) tour = generateInitialTour();
        if (tour.empty()) { emit finished(0,0); return; }

        double bestDist = totalDistance(tour);
        emitRouteUpdated(tour, bestDist, true);

        // Pre-processing
        std::vector<int> pos(n);
        auto refreshPos = [&]() { for (int i = 0; i < n; i++) pos[tour[i]] = i; };
        refreshPos();

        const int maxCand = m_params.value("lk_cand", 20).toInt();
        auto cand = getCandidates(maxCand);

        // Robust segment reverse for TSP tours (handles wrapping)
        auto flip = [&](int i, int j) {
            if (i == j) return;
            int len = (j - i + n) % n + 1;
            int half = len / 2;
            for (int k = 0; k < half; ++k) {
                int idx1 = (i + k) % n;
                int idx2 = (j - k + n) % n;
                std::swap(tour[idx1], tour[idx2]);
                pos[tour[idx1]] = idx1;
                pos[tour[idx2]] = idx2;
            }
        };

        bool improved = true;
        int iter = 0;
        const int maxIters = m_params.value("lk_iters", 1000).toInt();

        while (improved && iter < maxIters && !m_stopRequested) {
            improved = false;
            iter++;

            for (int i1 = 0; i1 < n && !m_stopRequested; i1++) {
                int t1 = tour[i1];
                int i2 = (i1 + 1) % n;
                int t2 = tour[i2];
                double g0 = dist(t1, t2);

                for (int t3 : cand[t2]) {
                    int i3 = pos[t3];
                    if (i3 == i1 || i3 == i2) continue;
                    double g1 = g0 - dist(t2, t3);
                    if (g1 <= 1e-9) continue;

                    // Standard 2-opt sequential closure: add (t3, t4) remove (t4, t1)
                    // In a tour t1-t2...t3-t4...
                    // t4 is neighbor of t3. Try t4 = tour[(i3+1)%n]
                    int i4 = (i3 + 1) % n;
                    int t4 = tour[i4];
                    double gain2 = g1 + (dist(t3, t4) - dist(t4, t1));

                    if (gain2 > 1e-7) {
                        flip(i2, i3);
                        bestDist = totalDistance(tour);
                        improved = true;
                        if (m_updateTimer.elapsed() > 200) { emitRouteUpdated(tour, bestDist); QThread::yieldCurrentThread(); }
                        goto next_iter;
                    }

                    // Try t4 = prev neighbor
                    i4 = (i3 - 1 + n) % n;
                    t4 = tour[i4];
                    gain2 = g1 + (dist(t3, t4) - dist(t4, t1));
                    if (gain2 > 1e-7) {
                        flip(std::min(i2, i3), std::max(i2, i3)); // Over-simplified flip but safe for now
                        bestDist = totalDistance(tour);
                        improved = true;
                        if (m_updateTimer.elapsed() > 200) { emitRouteUpdated(tour, bestDist); QThread::yieldCurrentThread(); }
                        goto next_iter;
                    }
                }
            }
            next_iter:;
            if (iter % 25 == 0) emit logMessage(QString("LK Iter %1 | Dist: %2").arg(iter).arg(bestDist, 0, 'f', 2));
        }

        bestDist = totalDistance(tour);
        emit logMessage(QString("LK Optimized | Final: %1").arg(bestDist, 0, 'f', 2));
        emitRouteUpdated(tour, bestDist, true);
        emit finished(bestDist, (int)timer.elapsed());
    } catch (...) { emit finished(0, 0); }
}

void SolverWorker::runGuidedLocalSearch(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }
        QElapsedTimer timer; timer.start();
        emit logMessage("Optimization in progress: Guided Local Search...");

        TRACE_MSG("Initializing GLS tour");
        std::vector<int> current = initialTour;
        if (current.size() != (size_t)n) current = generateInitialTour();
        if (current.empty()) { emit finished(0,0); return; }
        double currentDist = totalDistance(current);
        std::vector<int> bestRoute = current; double bestDist = currentDist;
        emitRouteUpdated(bestRoute, bestDist, true); // Initial baseline

        int candSize = m_params.value("global_cand_size", 25).toInt();
        auto cand = getCandidates(candSize);
        if (cand.empty()) { emit finished(bestDist, (int)timer.elapsed()); return; }

        int totalN = static_cast<int>(m_cities.size());
        std::vector<int> pos(totalN, -1);
        auto refreshPos = [&]() { for (int i = 0; i < n; i++) pos[current[i]] = i; };
        refreshPos();

        // Sparse edge penalties to avoid O(N^2) memory crash on large datasets
        std::unordered_map<uint64_t, int> penalties;
        auto getEdgeKey = [](int u, int v) -> uint64_t {
            if (u > v) std::swap(u, v);
            return (static_cast<uint64_t>(u) << 32) | static_cast<uint32_t>(v);
        };

        double lambdaParam = m_params.value("gls_lambda", 0.1).toDouble();
        int maxIters = m_params.value("gls_iters", 1000).toInt();
        double lambda = lambdaParam * (currentDist / std::max(1.0, (double)n));
        int itersSinceBest = 0;

        TRACE_MSG("Entering Guided Local Search iteration loop using: " + m_params.value("gls_strategy", "2-Opt Search").toString());
        QString strategy = m_params.value("gls_strategy", "2-Opt Search").toString();

        for (int iter = 0; iter < maxIters && !m_stopRequested; ++iter) {
            bool improved = true;
            while (improved && !m_stopRequested) {
                improved = false;
                std::mutex moveMutex;
                
                if (strategy == "2-Opt Search") {
                    #pragma omp parallel for shared(improved) schedule(dynamic, 100)
                    for (int i = 0; i < n; i++) {
                        if (improved || m_stopRequested) continue;
                        int a = current[i], b = current[(i + 1) % n];
                        uint64_t kab = getEdgeKey(a, b);
                        double d_ab = dist(a, b);
                        int pen_ab = penalties[kab];

                        for (int c_city : cand[a]) {
                            int pc = pos[c_city];
                            if (pc == i || pc == (i + 1) % n || ((pc + 1) % n) == i) continue;
                            int d_city = current[(pc + 1) % n];
                            uint64_t kcd = getEdgeKey(c_city, d_city);
                            uint64_t kac = getEdgeKey(a, c_city), kbd = getEdgeKey(b, d_city);

                            double d_gain = (d_ab + dist(c_city, d_city)) - (dist(a, c_city) + dist(b, d_city));
                            double p_gain = lambda * (pen_ab + penalties[kcd] - penalties[kac] - penalties[kbd]);

                            if (d_gain + p_gain > 1e-7) {
                                std::lock_guard<std::mutex> lock(moveMutex);
                                if (!improved) {
                                    int s = (i + 1) % n, e = pc;
                                    int rlen = (e - s + n) % n + 1;
                                    for (int k = 0; k < rlen/2; ++k) {
                                        int i1 = (s + k) % n, i2 = (e - k + n) % n;
                                        std::swap(current[i1], current[i2]);
                                        pos[current[i1]] = i1; pos[current[i2]] = i2;
                                    }
                                    currentDist -= d_gain; improved = true;
                                    if (currentDist < bestDist - 1e-7) { bestDist = currentDist; bestRoute = current; }
                                }
                                break;
                            }
                        }
                    }
                } else if (strategy == "3-Opt Search") {
                    // Similar logic for 3-Opt with penalties
                    for (int i = 0; i < n && !improved && !m_stopRequested; i++) {
                        int v1 = current[i], v2 = current[(i + 1) % n];
                        for (int v3 : cand[v1]) {
                            int p2 = pos[v3]; if (p2 == i || p2 == (i+1)%n) continue;
                            for (int v5 : cand[v2]) {
                                int p3 = pos[v5]; if (p3 == i || p3 == (i+1)%n || p3 == p2 || p3 == (p2 + 1) % n) continue;

                                int idxs[3] = { i, p2, p3 }; std::sort(idxs, idxs + 3);
                                int pA = idxs[0], pB = idxs[1], pC = idxs[2];
                                int u1 = current[pA], u2 = current[(pA+1)%n], u3 = current[pB], u4 = current[(pB+1)%n], u5 = current[pC], u6 = current[(pC+1)%n];

                                double base_d = dist(u1,u2) + dist(u3,u4) + dist(u5,u6);
                                double base_p = lambda * (penalties[getEdgeKey(u1,u2)] + penalties[getEdgeKey(u3,u4)] + penalties[getEdgeKey(u5,u6)]);
                                double bestAugGain = 0; int move = -1;

                                // Check 7 cases (3-opt uses 7 cases of reconnection)
                                auto checkCase = [&](int m, double d_sum, uint64_t k1, uint64_t k2, uint64_t k3) {
                                    double p_sum = lambda * (penalties[k1] + penalties[k2] + penalties[k3]);
                                    double gain = (base_d + base_p) - (d_sum + p_sum);
                                    if (gain > bestAugGain + 1e-7) { bestAugGain = gain; move = m; }
                                };
                                checkCase(1, dist(u1,u3)+dist(u2,u4)+dist(u5,u6), getEdgeKey(u1,u3), getEdgeKey(u2,u4), getEdgeKey(u5,u6));
                                checkCase(2, dist(u1,u2)+dist(u3,u5)+dist(u4,u6), getEdgeKey(u1,u2), getEdgeKey(u3,u5), getEdgeKey(u4,u6));
                                checkCase(3, dist(u1,u5)+dist(u3,u4)+dist(u2,u6), getEdgeKey(u1,u5), getEdgeKey(u3,u4), getEdgeKey(u2,u6));
                                checkCase(4, dist(u1,u4)+dist(u5,u2)+dist(u3,u6), getEdgeKey(u1,u4), getEdgeKey(u5,u2), getEdgeKey(u3,u6));
                                checkCase(5, dist(u1,u4)+dist(u5,u3)+dist(u2,u6), getEdgeKey(u1,u4), getEdgeKey(u5,u3), getEdgeKey(u2,u6));
                                checkCase(6, dist(u1,u5)+dist(u4,u2)+dist(u3,u6), getEdgeKey(u1,u5), getEdgeKey(u4,u2), getEdgeKey(u3,u6));
                                checkCase(7, dist(u1,u3)+dist(u5,u2)+dist(u4,u6), getEdgeKey(u1,u3), getEdgeKey(u5,u2), getEdgeKey(u4,u6));

                                if (move != -1) {
                                    auto reverse = [&](int s_idx, int e_idx) {
                                        int len = (e_idx - s_idx + n) % n + 1;
                                        for (int k=0; k<len/2; k++) {
                                            int i1=(s_idx+k)%n, i2=(e_idx-k+n)%n; std::swap(current[i1], current[i2]);
                                            pos[current[i1]]=i1; pos[current[i2]]=i2;
                                        }
                                    };
                                    double dBefore = dist(u1,u2)+dist(u3,u4)+dist(u5,u6);
                                    double dAfter = 0;
                                    switch(move) {
                                        case 1: reverse((pA+1)%n, pB); dAfter = dist(u1,u3)+dist(u2,u4)+dist(u5,u6); break;
                                        case 2: reverse((pB+1)%n, pC); dAfter = dist(u1,u2)+dist(u3,u5)+dist(u4,u6); break;
                                        case 3: reverse((pA+1)%n, pC); dAfter = dist(u1,u5)+dist(u3,u4)+dist(u2,u6); break;
                                        case 4: reverse((pA+1)%n, pB); reverse((pB+1)%n, pC); reverse((pA+1)%n, pC); dAfter = dist(u1,u4)+dist(u5,u2)+dist(u3,u6); break;
                                        case 5: reverse((pB+1)%n, pC); reverse((pA+1)%n, pC); dAfter = dist(u1,u4)+dist(u5,u3)+dist(u2,u6); break;
                                        case 6: reverse((pA+1)%n, pB); reverse((pA+1)%n, pC); dAfter = dist(u1,u5)+dist(u4,u2)+dist(u3,u6); break;
                                        case 7: reverse((pA+1)%n, pB); reverse((pB+1)%n, pC); dAfter = dist(u1,u3)+dist(u5,u2)+dist(u4,u6); break;
                                    }
                                    currentDist -= (dBefore - dAfter); improved = true;
                                    if (currentDist < bestDist - 1e-7) { bestDist = currentDist; bestRoute = current; }
                                    break;
                                }
                            }
                        }
                    }
                } else if (strategy == "5-Opt Search") {
                    // Multi-bridge with penalties
                    for (int i = 0; i < n && !improved && !m_stopRequested; i++) {
                       int v1 = current[i];
                       for (int v3 : cand[v1]) {
                            int p2 = pos[v3]; if (p2 == i || p2 == (i+1)%n || p2 == (i-1+n)%n) continue;
                            for (int v5 : cand[v3]) {
                                int p3 = pos[v5]; if (p3 == i || p3 == (i+1)%n || p3 == p2 || p3 == (p2+1)%n) continue;
                                for (int v7 : cand[v5]) {
                                    int p4 = pos[v7]; if (p4 == i || p4 == (i+1)%n || p4 == p2 || p4 == (p2+1)%n || p4 == p3 || p4 == (p3+1)%n) continue;
                                    for (int v9 : cand[v1]) {
                                        int p5 = pos[v9]; if (p5 == i || p2 == p5 || p3 == p5 || p4 == p5) continue;
                                        int idxs[5] = { i, p2, p3, p4, p5 }; std::sort(idxs, idxs+5);
                                        int t1 = idxs[0], t2 = idxs[1], t3 = idxs[2], t4 = idxs[3], t5 = idxs[4];
                                        if (t2-t1 < 2 || t3-t2 < 2 || t4-t3 < 2 || t5-t4 < 2 || (n-1-t5+t1) < 1) continue;
                                        int u1 = current[t1], u2 = current[(t1+1)%n], u3 = current[t2], u4 = current[(t2+1)%n];
                                        int u5 = current[t3], u6 = current[(t3+1)%n], u7 = current[t4], u8 = current[(t4+1)%n];
                                        int u9 = current[t5], u10 = current[(t5+1)%n];
                                        double cur_d = dist(u1,u2) + dist(u3,u4) + dist(u5,u6) + dist(u7,u8) + dist(u9,u10);
                                        double cur_p = lambda * (penalties[getEdgeKey(u1,u2)] + penalties[getEdgeKey(u3,u4)] + penalties[getEdgeKey(u5,u6)] + penalties[getEdgeKey(u7,u8)] + penalties[getEdgeKey(u9,u10)]);
                                        double new_d = dist(u1,u7) + dist(u8,u3) + dist(u4,u9) + dist(u10,u5) + dist(u6,u2);
                                        double new_p = lambda * (penalties[getEdgeKey(u1,u7)] + penalties[getEdgeKey(u8,u3)] + penalties[getEdgeKey(u4,u9)] + penalties[getEdgeKey(u10,u5)] + penalties[getEdgeKey(u6,u2)]);
                                        if ((cur_d + cur_p) - (new_d + new_p) > 1e-7) {
                                            std::vector<int> nr; nr.reserve(n);
                                            for(int x=0; x<=t1; x++) nr.push_back(current[x]);
                                            for(int x=t3+1; x<=t4; x++) nr.push_back(current[x]);
                                            for(int x=t1+1; x<=t2; x++) nr.push_back(current[x]);
                                            for(int x=t4+1; x<=t5; x++) nr.push_back(current[x]);
                                            for(int x=t2+1; x<=t3; x++) nr.push_back(current[x]);
                                            for(int x=t5+1; x<n; x++) nr.push_back(current[x]);
                                            current = std::move(nr); refreshPos(); currentDist = totalDistance(current);
                                            improved = true; if (currentDist < bestDist - 1e-7) { bestDist = currentDist; bestRoute = current; }
                                            break;
                                        }
                                    }
                                    if (improved) break;
                                }
                                if (improved) break;
                            }
                            if (improved) break;
                       }
                    }
                }
                if (improved) emitRouteUpdated(current, bestDist);
            }

            // Convergence Kick logic: if no global improvement for X iterations, apply a perturbation
            if (currentDist < bestDist - 1e-7) {
                bestDist = currentDist; bestRoute = current;
                itersSinceBest = 0;
            } else {
                itersSinceBest++;
                int kickInterval = m_params.value("gls_kick_interval", 50).toInt();
                if (kickInterval > 0 && itersSinceBest >= kickInterval) {
                   emit logMessage("GLS Stalled -> Running ILS kick sub-procedure...");
                   int ilsSteps = m_params.value("gls_kick_steps", 100).toInt();
                   double ilsKickFract = m_params.value("gls_kick_size", 0.15).toDouble();
                   performILS(current, currentDist, ilsSteps, ilsKickFract);
                   refreshPos();
                   if (currentDist < bestDist - 1e-7) { bestDist = currentDist; bestRoute = current; }
                   emitRouteUpdated(current, bestDist);
                   itersSinceBest = 0;
                }
            }

            // AGGRESSIVE Augmentation: Penalize Top 5% by Utility
            struct UtilEdge { int u, v; double util; };
            std::vector<UtilEdge> edgeList; edgeList.reserve(n);
            for (int i = 0; i < n; i++) {
                int u = current[i], v = current[(i+1)%n];
                double util = dist(u, v) / (1.0 + (double)penalties[getEdgeKey(u, v)]);
                edgeList.push_back({u, v, util});
            }
            std::sort(edgeList.begin(), edgeList.end(), [](const UtilEdge& a, const UtilEdge& b){ return a.util > b.util; });
            int numToPenalize = std::min(50, std::max(1, (int)(n * 0.05)));
            for (int i = 0; i < numToPenalize; i++) penalties[getEdgeKey(edgeList[i].u, edgeList[i].v)]++;
            
            if (iter % 20 == 0) emit logMessage(QString("GLS Iter %1 | Best: %2 | Divergence: %3").arg(iter).arg(bestDist, 0, 'f', 2).arg(itersSinceBest));
        }
        emit finished(bestDist, (int)timer.elapsed());
    } catch (const std::exception& e) {
        emit logMessage(QString("Solver Error: %1").arg(e.what()));
        emit finished(0, 0);
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runWhaleOptimization(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Pro Memetic Whale Optimization (WOA)...");

        const int SIZE = m_params.value("woa_size", 40).toInt();
        const int ITERS = m_params.value("woa_iters", 1000).toInt();

        TRACE_MSG("Initializing Whale population");
        std::vector<std::vector<int>> whales(SIZE);
        std::vector<double> lengths(SIZE);

        whales.at(0) = initialTour;
        if (whales.at(0).size() != (size_t)n) whales.at(0) = generateInitialTour();
        if (whales.at(0).empty()) { emit finished(0,0); return; }

        lengths.at(0) = totalDistance(whales.at(0));
        optimizeTwoOpt(whales.at(0), lengths.at(0), true, true);

        for (int i = 1; i < SIZE; i++) {
            whales.at(i) = whales.at(0);
            // Small logarithmic perturbation instead of linear fraction
            int numSwaps = std::max(2, (int)std::log10(n) * 2);
            for(int s=0; s<numSwaps; s++) {
                int i1 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                int i2 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                std::swap(whales.at(i).at(i1), whales.at(i).at(i2));
            }
            lengths.at(i) = totalDistance(whales.at(i));
            if (i % 5 == 0) optimizeTwoOpt(whales.at(i), lengths.at(i), true, true);
            if (m_stopRequested) { emit finished(lengths.at(0), (int)timer.elapsed()); return; }
        }

        double bestGlobal = lengths.at(0);
        std::vector<int> bestWhale = whales.at(0);
        emitRouteUpdated(bestWhale, bestGlobal, true);

        std::uniform_real_distribution<double> dReal(0.0, 1.0);
        std::uniform_int_distribution<int> dCity(0, n - 1), dWhale(0, SIZE - 1);

        QString variant = m_params.value("woa_variant", "Standard Memetic (S-WOA)").toString();
        emit logMessage("Selected Variant: " + variant);

        TRACE_MSG("Entering Multi-Variant Whale iteration loop");
        for (int iter = 0; iter < ITERS && !m_stopRequested; iter++) {

            double a = 2.0 - iter * (2.0 / (double)ITERS);
            if (variant == "Adaptive Weight (AW-WOA)") {
                a = 2.0 * std::exp(-5.0 * ((double)iter / (double)ITERS));
            }

            for (int i = 0; i < SIZE && !m_stopRequested; i++) {
                double r = dReal(m_rng);
                double p = dReal(m_rng);

                if (variant == "Chaotic Bubble-Net (CB-WOA)") {
                    static double chaos_p = 0.44;
                    chaos_p = 4.0 * chaos_p * (1.0 - chaos_p);
                    p = chaos_p;
                }

                double A = 2.0 * a * r - a;
                std::vector<int> next = whales.at(i);

                if (p < 0.5) {
                    if (std::abs(A) < 1.0) {
                        if (variant == "Oppositional Based (OBL-WOA)" && dReal(m_rng) < 0.15) {
                            std::reverse(next.begin(), next.end());
                            std::rotate(next.begin(), next.begin() + dWhale(m_rng) % n, next.end());
                        } else {
                            next = crossoverOX(whales.at(i), bestWhale);
                        }
                    } else {
                        int randIdx = dWhale(m_rng);
                        if (variant == "Levy Flight Attack (LF-WOA)") {
                            int jumps = (dReal(m_rng) < 0.7) ? 1 : (dReal(m_rng) < 0.95 ? 3 : 8);
                            for (int j=0; j<jumps; ++j) {
                                int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                                std::reverse(next.begin() + s1, next.begin() + s2 + 1);
                            }
                        } else {
                            next = crossoverOX(whales.at(i), whales.at(randIdx));
                        }
                    }
                } else {
                    int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                    if (dReal(m_rng) < 0.5) {
                        std::reverse(next.begin() + s1, next.begin() + s2 + 1);
                    } else {
                        std::rotate(next.begin() + s1, next.begin() + (s1 + s2) / 2, next.begin() + s2 + 1);
                    }
                }

                double nextLen = totalDistance(next);

                if (nextLen < bestGlobal * 1.05 || iter % 50 == 0) {
                    optimizeTwoOpt(next, nextLen, true, true);
                }

                if (nextLen < lengths.at(i)) {
                    whales.at(i) = std::move(next);
                    lengths.at(i) = nextLen;

                    if (nextLen < bestGlobal - 1e-9) {
                        bestGlobal = nextLen;
                        bestWhale = whales.at(i);
                        if (m_updateTimer.elapsed() > 100) {
                            emitRouteUpdated(bestWhale, bestGlobal, false);
                            QThread::yieldCurrentThread();
                        }
                    }
                }
            }

            // ILS-Guided Leader Extension
            if (variant == "ILS-Guided Leader (ILS-WOA)" && dReal(m_rng) < 0.15) {
                std::vector<int> candidate = bestWhale;
                double dCand = bestGlobal;
                int kSteps = m_params.value("woa_kick_steps", 50).toInt();
                double kSize = m_params.value("woa_kick_size", 0.15).toDouble();
                performILS(candidate, dCand, kSteps, kSize);
                if (dCand < bestGlobal) {
                    bestGlobal = dCand; bestWhale = candidate;
                    whales.at(0) = bestWhale; lengths.at(0) = bestGlobal;
                }
            }

            if (iter % 100 == 0) {
                emit logMessage(QString("WOA Iter %1 | Best: %2").arg(iter).arg(bestGlobal, 0, 'f', 2));
            }
        }
        emit finished(bestGlobal, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

// Utility: Order Crossover (OX) helper for WOA/GA
std::vector<int> SolverWorker::crossoverOX(const std::vector<int>& p1, const std::vector<int>& p2) {
    TRACE_SCOPE;
    int n = static_cast<int>(p1.size());
    std::vector<int> child(n, -1);
    std::vector<bool> used(n, false);

    std::uniform_int_distribution<int> dCity(0, n - 1);
    int s = dCity(m_rng), e = dCity(m_rng); if (s > e) std::swap(s, e);

    for (int i = s; i <= e; i++) {
        child.at(i) = p1.at(i);
        used.at(child.at(i)) = true;
    }

    int p2p = (e + 1) % n, cp = (e + 1) % n;
    int filled = (e - s + 1);
    while (filled < n) {
        int node = p2.at(p2p);
        if (!used.at(node)) {
            child.at(cp) = node;
            used.at(node) = true;
            cp = (cp + 1) % n;
            filled++;
        }
        p2p = (p2p + 1) % n;
    }
    return child;
}


std::vector<SolverWorker::Edge> SolverWorker::getMstEdges() const {

    TRACE_SCOPE;
    int n = static_cast<int>(m_cities.size());
    if (n < 2) return {};

    std::vector<Edge> allEdges;
    if (!m_delaunayEdges.empty()) {
        for (size_t i = 0; i < m_delaunayEdges.size(); i += 2) {
            int u = (int)m_delaunayEdges[i];
            int v = (int)m_delaunayEdges[i + 1];
            allEdges.push_back({u, v, dist(u, v)});
        }
    } else {
        // Fallback to O(N^2) - aborted for safety if N is large
        if (n > 5000) {
            TRACE_MSG("Too many cities for O(N^2) MST fallback. Aborting.");
            return {};
        }
        for (int i = 0; i < n && !m_stopRequested; ++i) {
            for (int j = i + 1; j < n; ++j) {
                allEdges.push_back({i, j, dist(i, j)});
            }
        }
    }
    if (m_stopRequested) return {};
    std::sort(allEdges.begin(), allEdges.end());

    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);

    // Iterative find with path compression to avoid stack overflow on large datasets
    auto find = [&](int i) -> int {
        int root = i;
        while (parent[root] != root) root = parent[root];
        while (parent[i] != root) {
            int next = parent[i];
            parent[i] = root;
            i = next;
        }
        return root;
    };

    std::vector<Edge> mst;
    for (const auto& e : allEdges) {
        int rootU = find(e.u);
        int rootV = find(e.v);
        if (rootU != rootV) {
            mst.push_back(e);
            parent[rootU] = rootV;
            if ((int)mst.size() == n - 1) break;
        }
    }
    return mst;
}

std::vector<int> SolverWorker::christofidesTour() {
    TRACE_SCOPE;
    int n = static_cast<int>(m_cities.size());
    if (n < 2) return {0};
    if (n == 2) return {0, 1};

    // 1. MST
    std::vector<Edge> mst = getMstEdges();
    if (m_stopRequested || mst.empty()) return {};

    // 2. Odd-degree vertices
    std::vector<int> degree(n, 0);
    for (const auto& e : mst) { degree[e.u]++; degree[e.v]++; }
    std::vector<int> odd;
    for (int i = 0; i < n; i++) if (degree[i] % 2 != 0) odd.push_back(i);

    // 3. Fast Greedy Matching for odd-degree vertices using KD-tree (O(M log M))
    std::vector<Edge> matching;
    std::vector<bool> vertexMatched(n, false);
    CDT::KDTree::KDTree<double> oddTree;
    for (int vIdx : odd) oddTree.insert(static_cast<CDT::VertInd>(vIdx), m_cities);

    for (int u : odd) {
        if (m_stopRequested) break;
        if (vertexMatched[u]) continue;

        if (m_updateTimer.elapsed() > 500) { // Throttle log messages
            emit logMessage(QString("Matching Progress: %1/%2").arg(std::count(vertexMatched.begin(), vertexMatched.end(), true)).arg(odd.size()));
            m_updateTimer.restart();
        }
        
        auto result = oddTree.nearestFiltered(
            CDT::V2d<double>(m_cities[u].x, m_cities[u].y),
            m_cities,
            [&](CDT::VertInd idx) {
                return (int)idx != u && !vertexMatched[(int)idx];
            }
        );

        if (result.second != static_cast<CDT::VertInd>(-1)) {
            int v = (int)result.second;
            vertexMatched[u] = vertexMatched[v] = true;
            matching.push_back({u, v, dist(u, v)});
        }
    }
    if (m_stopRequested) return {};

    // 4. Combined Multigraph (Adjacency List)
    std::vector<std::vector<int>> adj(n);
    for (const auto& e : mst) { adj[e.u].push_back(e.v); adj[e.v].push_back(e.u); }
    for (const auto& e : matching) { adj[e.u].push_back(e.v); adj[e.v].push_back(e.u); }

    // 5. Hierholzer for Eulerian Circuit
    std::vector<int> circuit;
    std::stack<int> s;
    s.push(0);

    std::vector<std::vector<int>> currentAdj = adj;
    while (!s.empty() && !m_stopRequested) {
        int u = s.top();
        if (currentAdj[u].empty()) {
            circuit.push_back(u);
            s.pop();
        } else {
            int v = currentAdj[u].back();
            currentAdj[u].pop_back();
            // Remove reverse edge
            auto it = std::find(currentAdj[v].begin(), currentAdj[v].end(), u);
            if (it != currentAdj[v].end()) currentAdj[v].erase(it);
            s.push(v);
        }
    }
    if (m_stopRequested) return {};

    // 6. Shortcut to Hamiltonian Path
    std::vector<int> tour;
    std::vector<bool> visited(n, false);
    for (int node : circuit) {
        if (!visited[node]) {
            tour.push_back(node);
            visited[node] = true;
        }
    }

    if (tour.size() < (size_t)n) {
        for (int i=0; i<n; i++) if (!visited[i]) tour.push_back(i);
    }

    return tour;
}

void SolverWorker::runPelicanOptimization(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0,0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Pelican Optimization Algorithm (POA)...");

        const int SIZE = m_params.value("poa_size", 40).toInt();
        const int ITERS = m_params.value("poa_iters", 500).toInt();
        QString variant = m_params.value("poa_variant", "Standard Memetic (S-POA)").toString();
        emit logMessage("Selected Variant: " + variant);

        std::vector<std::vector<int>> pelicans(SIZE);
        std::vector<double> lengths(SIZE);

        pelicans.at(0) = initialTour;
        if (pelicans.at(0).size() != (size_t)n) pelicans.at(0) = generateInitialTour();
        if (pelicans.at(0).empty()) { emit finished(0,0); return; }

        lengths.at(0) = totalDistance(pelicans.at(0));
        optimizeTwoOpt(pelicans.at(0), lengths.at(0), true, true);

        for (int i = 1; i < SIZE && !m_stopRequested; i++) {
            pelicans.at(i) = pelicans.at(0);
            // Gentle perturbation preserving structure
            int numSwaps = std::max(2, (int)std::log10(n) * 2);
            for(int s=0; s<numSwaps; s++) {
                int i1 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                int i2 = std::uniform_int_distribution<int>(0, n-1)(m_rng);
                std::swap(pelicans.at(i).at(i1), pelicans.at(i).at(i2));
            }
            lengths.at(i) = totalDistance(pelicans.at(i));
            if (i % 5 == 0) optimizeTwoOpt(pelicans.at(i), lengths.at(i), true, true);
        }

        auto sortPelicans = [&]() {
            std::vector<int> idx(SIZE); std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return lengths.at(a) < lengths.at(b); });
            std::vector<std::vector<int>> np(SIZE); std::vector<double> nl(SIZE);
            for (int i = 0; i < SIZE; i++) { np.at(i) = pelicans.at(idx.at(i)); nl.at(i) = lengths.at(idx.at(i)); }
            pelicans = np; lengths = nl;
        };
        sortPelicans();

        double bestGlobal = lengths.at(0);
        std::vector<int> bestPelican = pelicans.at(0);
        emitRouteUpdated(bestPelican, bestGlobal, true);

        std::uniform_real_distribution<double> dReal(0.0, 1.0);
        std::uniform_int_distribution<int> dCity(0, n - 1), dPelican(0, SIZE - 1);

        TRACE_MSG("Entering Pelican iteration loop");
        for (int iter = 0; iter < ITERS && !m_stopRequested; iter++) {

            for (int i = 0; i < SIZE && !m_stopRequested; i++) {
                std::vector<int> next = pelicans.at(i);
                int targetIdx = dPelican(m_rng);
                if (variant == "Standard Memetic (S-POA)") targetIdx = 0;

                bool doPhase1 = true, doPhase2 = true;
                if (variant == "Adaptive Phase (AP-POA)") {
                    double pPhase = 1.0 - (double)iter / ITERS; // High Phase 1 early, low Phase 1 late
                    if (dReal(m_rng) > pPhase) doPhase1 = false;
                    else doPhase2 = false;
                }

                // Phase 1: Moving towards prey (exploration)
                if (doPhase1) {
                    if (variant == "Levy Flight Search (LF-POA)" && dReal(m_rng) < 0.2) {
                        int jumps = (dReal(m_rng) < 0.7) ? 2 : 6;
                        for (int j=0; j<jumps; ++j) {
                            int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                            std::reverse(next.begin() + s1, next.begin() + s2 + 1);
                        }
                    } else if (variant == "Oppositional Based (OBL-POA)" && dReal(m_rng) < 0.1) {
                        std::reverse(next.begin(), next.end());
                    } else {
                        next = crossoverOX(pelicans.at(i), pelicans.at(targetIdx));
                    }

                    double nextLen = totalDistance(next);
                    if (nextLen < lengths.at(i)) {
                        pelicans.at(i) = next; lengths.at(i) = nextLen;
                    } else {
                        next = pelicans.at(i); // revert if phase 1 failed, because phase 2 builds on it
                    }
                }

                // Phase 2: Winging on the water surface (exploitation)
                if (doPhase2) {
                    double R = 0.2;
                    if (variant == "Chaotic Pelican Swarm (CP-POA)") {
                        static double chaos = 0.55;
                        chaos = 4.0 * chaos * (1.0 - chaos);
                        R = chaos;
                    }

                    if (dReal(m_rng) < R) { // Search local neighborhood
                        int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                        std::rotate(next.begin() + s1, next.begin() + (s1+s2)/2, next.begin() + s2 + 1);
                    } else {
                        int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                        std::reverse(next.begin() + s1, next.begin() + s2 + 1);
                    }

                    double nextLen = totalDistance(next);
                    if (nextLen < bestGlobal * 1.05 || iter % 50 == 0) {
                        optimizeTwoOpt(next, nextLen, true, true);
                    }

                    if (nextLen < lengths.at(i)) {
                        pelicans.at(i) = std::move(next);
                        lengths.at(i) = nextLen;
                    }
                }

                // Update global best
                if (lengths.at(i) < bestGlobal - 1e-9) {
                    bestGlobal = lengths.at(i);
                    bestPelican = pelicans.at(i);
                    if (m_updateTimer.elapsed() > 100) {
                        emitRouteUpdated(bestPelican, bestGlobal, false);
                        QThread::yieldCurrentThread();
                    }
                }
            }
            sortPelicans();
            
            // ILS-Guided Search Extension
            if (variant == "ILS-Guided Search (ILS-POA)" && dReal(m_rng) < 0.15) {
                std::vector<int> candidate = bestPelican;
                double dCand = bestGlobal;
                int kSteps = m_params.value("poa_kick_steps", 50).toInt();
                double kSize = m_params.value("poa_kick_size", 0.15).toDouble();
                performILS(candidate, dCand, kSteps, kSize);
                if (dCand < bestGlobal) {
                    bestGlobal = dCand; bestPelican = candidate;
                    pelicans.at(0) = bestPelican; lengths.at(0) = bestGlobal;
                }
            }
            if (iter % 100 == 0) emit logMessage(QString("POA Iter %1 | Best: %2").arg(iter).arg(bestGlobal, 0, 'f', 2));
        }

        emitRouteUpdated(bestPelican, bestGlobal, true);
        emit finished(bestGlobal, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}

void SolverWorker::runEcologicalCycleOptimizer(const std::vector<int>& initialTour) {
    TRACE_SCOPE;
    try {
        int n = static_cast<int>(m_cities.size());
        if (n < 3) { emit finished(0, 0); return; }
        if (n < 4) { runTwoOpt(initialTour); return; }

        QElapsedTimer timer; timer.start();
        emit logMessage("Starting Ecological Cycle Optimizer (ECO)...");

        const int SIZE = m_params.value("eco_size", 50).toInt();
        const int ITERS = m_params.value("eco_iters", 1000).toInt();
        double P_RATIO = m_params.value("eco_producers", 0.20).toDouble();
        
        QString variant = m_params.value("eco_variant", "Standard Memetic (S-ECO)").toString();
        emit logMessage("Selected Variant: " + variant);

        std::vector<std::vector<int>> pop(SIZE);
        std::vector<double> energy(SIZE);

        pop.at(0) = initialTour;
        if (pop.at(0).size() != (size_t)n) pop.at(0) = generateInitialTour();
        if (pop.at(0).empty()) { emit finished(0, 0); return; }

        energy.at(0) = totalDistance(pop.at(0));
        optimizeTwoOpt(pop.at(0), energy.at(0), true, true);
        
        std::uniform_int_distribution<int> dCity(0, n - 1);
        std::uniform_real_distribution<double> dReal(0.0, 1.0);

        for (int i = 1; i < SIZE && !m_stopRequested; i++) {
            pop.at(i) = pop.at(0);
            int numSwaps = std::max(2, (int)std::log10(n) * 2);
            for(int s=0; s<numSwaps; s++) {
                int i1 = dCity(m_rng), i2 = dCity(m_rng);
                std::swap(pop.at(i).at(i1), pop.at(i).at(i2));
            }
            energy.at(i) = totalDistance(pop.at(i));
            if (i % 10 == 0) optimizeTwoOpt(pop.at(i), energy.at(i), true, true);
        }

        auto sortEcosystem = [&]() {
            std::vector<int> idx(SIZE); std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return energy.at(a) < energy.at(b); });
            std::vector<std::vector<int>> np(SIZE); std::vector<double> ne(SIZE);
            for (int i = 0; i < SIZE; i++) { np.at(i) = pop.at(idx.at(i)); ne.at(i) = energy.at(idx.at(i)); }
            pop = np; energy = ne;
        };
        sortEcosystem();

        double bestGlobal = energy.at(0);
        std::vector<int> bestTour = pop.at(0);
        emitRouteUpdated(bestTour, bestGlobal, true);

        for (int iter = 0; iter < ITERS && !m_stopRequested; iter++) {
            double currentProducerRatio = P_RATIO;
            if (variant == "Dynamic Trophic Levels (DTL-ECO)") {
                currentProducerRatio = 0.1 + 0.4 * (1.0 - (double)iter / ITERS);
            }

            int numProducers = std::max(1, (int)(SIZE * currentProducerRatio));
            
            for (int i = 0; i < numProducers && !m_stopRequested; i++) {
                if (dReal(m_rng) < 0.1) {
                    int s1 = dCity(m_rng), s2 = dCity(m_rng); if (s1 > s2) std::swap(s1, s2);
                    std::reverse(pop.at(i).begin() + s1, pop.at(i).begin() + s2 + 1);
                    energy.at(i) = totalDistance(pop.at(i));
                }
                optimizeTwoOpt(pop.at(i), energy.at(i), true, true);
            }

            for (int i = numProducers; i < SIZE && !m_stopRequested; i++) {
                int targetIdx = std::uniform_int_distribution<int>(0, numProducers - 1)(m_rng);
                std::vector<int> next = crossoverOX(pop.at(i), pop.at(targetIdx));
                double nE = totalDistance(next);
                if (nE < bestGlobal * 1.05 || iter % 100 == 0) optimizeTwoOpt(next, nE, true, true);
                if (nE < energy.at(i)) { pop.at(i) = std::move(next); energy.at(i) = nE; }
            }

            if (variant == "ILS-Guided Recycling (ILS-ECO)" || dReal(m_rng) < 0.05) {
                int weakIdx = SIZE - 1; 
                pop.at(weakIdx) = bestTour; 
                energy.at(weakIdx) = bestGlobal;
                int kSteps = m_params.value("eco_kick_steps", 50).toInt();
                double kSize = m_params.value("eco_kick_size", 0.15).toDouble();
                performILS(pop.at(weakIdx), energy.at(weakIdx), kSteps, kSize);
            }

            sortEcosystem();

            if (energy.at(0) < bestGlobal - 1e-9) {
                bestGlobal = energy.at(0);
                bestTour = pop.at(0);
                if (m_updateTimer.elapsed() > 100) emitRouteUpdated(bestTour, bestGlobal, false);
            }

            if (iter % 100 == 0) emit logMessage(QString("ECO Iter %1 | Best: %2 | Producers: %3").arg(iter).arg(bestGlobal, 0, 'f', 2).arg(numProducers));
        }

        emitRouteUpdated(bestTour, bestGlobal, true);
        emit finished(bestGlobal, (int)timer.elapsed());
    } catch (...) {
        emit finished(0, 0);
    }
}


