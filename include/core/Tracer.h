/**
 * @file Tracer.h
 * @author Mohamed Elkeran
 * @license MIT (c) 2026
 */
#pragma once

#include <QString>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <QMutex>

/**
 * @brief Unified Tracing System for TSP Studio
 * Provides RAII-based function entry/exit logging and manual stage tracing.
 */
class Tracer {
public:
    static void init(const QString& logFile = "tsp_trace.log") {
        QMutexLocker locker(&s_mutex);
        s_logFile = logFile;
        QFile file(s_logFile);
        (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate); // Clear existing log
        file.close();
    }

    static void setEnabled(bool e) { s_enabled = e; }
    static bool isEnabled() { return s_enabled; }

    Tracer(const char* func, const char* file, int line) 
        : m_func(func) {
        if (s_enabled) log(QString("ENTER: %1 (%2:%3)").arg(m_func).arg(file).arg(line));
    }
    
    ~Tracer() {
        if (s_enabled) log(QString("EXIT:  %1").arg(m_func));
    }
    
    static void stage(const char* func, const QString& msg) {
        if (s_enabled) log(QString("STAGE: %1 -> %2").arg(func).arg(msg));
    }

private:
    static void log(const QString& rawMsg) {
        QMutexLocker locker(&s_mutex);
        if (!s_enabled) return;
        
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        QString fullMsg = QString("[%1] [TRACE] %2").arg(ts).arg(rawMsg);
        
        if (!s_logFile.isEmpty()) {
            QFile file(s_logFile);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&file);
                out << fullMsg << "\n";
                file.close();
            }
        }
    }

    const char* m_func;
    inline static QString s_logFile = "";
    inline static QMutex s_mutex;
    inline static bool s_enabled = false;
};

#define TRACE_SCOPE Tracer _tracer(__FUNCTION__, __FILE__, __LINE__)
#define TRACE_MSG(m) Tracer::stage(__FUNCTION__, m)
