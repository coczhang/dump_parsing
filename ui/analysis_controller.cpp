#include "analysis_controller.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>

AnalysisController::AnalysisController(QObject *parent)
    : QObject(parent)
{
}

AnalysisController::~AnalysisController()
{
    // 进程退出或窗口销毁时，尽量优雅地等待后台分析线程结束，
    // 避免线程仍在访问已释放的 QObject。
    if (analysisThread && analysisThread->isRunning()) {
        analysisThread->requestInterruption();
        analysisThread->wait();
    }
}

bool AnalysisController::isRunning() const
{
    return running;
}

void AnalysisController::startAnalysis(const QString &dumpPath,
                                       const QString &symbolPath,
                                       const QString &sourceRootPath)
{
    // 控制器层做“单次执行”保护，防止用户重复点击导致并发分析。
    if (running) {
        return;
    }

    setRunning(true);
    emit analysisStarted();

    // 用 QPointer 防止 this 已销毁时仍回调 UI。
    QPointer<AnalysisController> self(this);
    analysisThread = QThread::create([self, dumpPath, symbolPath, sourceRootPath]() {
        // 真正耗时逻辑放到工作线程，避免阻塞 UI 事件循环。
        CrashAnalyzer analyzer;
        const CrashReport report = analyzer.analyze(dumpPath, symbolPath, sourceRootPath);

        if (!self) {
            return;
        }

        // 结果通过 QueuedConnection 切回主线程分发，保证 UI 线程安全。
        QMetaObject::invokeMethod(
            self,
            [self, report]() {
                if (!self) {
                    return;
                }
                emit self->analysisFinished(report);
            },
            Qt::QueuedConnection);
    });

    // 线程结束后统一回收状态和对象，避免泄漏。
    connect(analysisThread, &QThread::finished, this, [this]() {
        analysisThread = nullptr;
        setRunning(false);
    });
    connect(analysisThread, &QThread::finished, analysisThread, &QObject::deleteLater);
    analysisThread->start();
}

void AnalysisController::setRunning(bool value)
{
    if (running == value) {
        return;
    }
    running = value;
    emit runningChanged(running);
}
