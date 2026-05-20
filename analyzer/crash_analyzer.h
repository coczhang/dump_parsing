#ifndef CRASH_ANALYZER_H
#define CRASH_ANALYZER_H

#include <QString>
#include <QStringList>
#include <QVector>

struct CrashStackFrame
{
    quint64 address = 0;
    quint64 moduleBase = 0;
    quint64 moduleOffset = 0;
    QString moduleName;
    QString functionName;
    QString sourceFile;
    int line = 0;
    bool isCrashFrame = false;
};

struct CrashReport
{
    bool ok = false;
    QString errorMessage;
    QString dumpPath;
    QString symbolPath;
    QString sourceRootPath;
    QString exceptionCode;
    QString exceptionDescription;
    quint64 exceptionAddress = 0;
    quint64 crashModuleBase = 0;
    quint64 crashAddressOffset = 0;
    QString exceptionModule;
    QString crashFunction;
    QString crashFunctionOffset;
    QString crashLocation;
    QString crashFrameLine;
    QString crashSourcePath;
    QString crashSourceCodeHtml;
    int crashSourceLine = 0;
    int crashSourceStartLine = 0;
    QString winDbgReportPath;
    QString resolvedReportPath;
    QString winDbgReportText;
    QString resolvedReportText;
    QString scriptStdout;
    quint32 crashThreadId = 0;
    QVector<CrashStackFrame> frames;
    QStringList warnings;

    QString toHtml() const;
};

class CrashAnalyzer
{
public:
    CrashReport analyze(const QString &dumpPath,
                        const QString &symbolPath,
                        const QString &sourceRootPath = QString());
};

#endif // CRASH_ANALYZER_H
