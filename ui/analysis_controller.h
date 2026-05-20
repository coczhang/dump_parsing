#ifndef ANALYSIS_CONTROLLER_H
#define ANALYSIS_CONTROLLER_H

#include "crash_analyzer.h"

#include <QObject>

class QThread;

class AnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisController(QObject *parent = nullptr);
    ~AnalysisController() override;

    bool isRunning() const;
    void startAnalysis(const QString &dumpPath,
                       const QString &symbolPath,
                       const QString &sourceRootPath);

signals:
    void analysisStarted();
    void analysisFinished(const CrashReport &report);
    void runningChanged(bool running);

private:
    void setRunning(bool running);

    QThread *analysisThread = nullptr;
    bool running = false;
};

#endif // ANALYSIS_CONTROLLER_H
