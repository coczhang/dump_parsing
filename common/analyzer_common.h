#ifndef ANALYZER_COMMON_H
#define ANALYZER_COMMON_H

#include <QByteArray>
#include <QString>
#include <QStringList>

struct ProcessResult
{
    bool ok = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    QString errorMessage;
};

struct ParsedExceptionSummary
{
    QString code;
    QString description;
};

QString analyzerDecodeText(const QByteArray &data);
QString analyzerReadTextFile(const QString &path);
bool analyzerWriteTextFile(const QString &path, const QString &text, QString *error);
QString analyzerSanitizeFileName(QString value);
QString analyzerRuntimeRoot();
QString analyzerToolPath(const QString &rootPath, const QString &toolName);
ProcessResult analyzerRunCommandScript(const QString &scriptPath,
                                       const QStringList &arguments,
                                       const QString &workingDirectory);
QString analyzerSymbolModuleName(const QString &symbolPath, bool includeExeExtension);
QString analyzerInferExecutablePath(const QString &symbolPath);
QString analyzerExtractFirstValue(const QString &text, const QString &key);
QString analyzerExtractModuleOffset(const QString &text);
QString analyzerNormalizeExceptionCodeFromText(QString codeText);
ParsedExceptionSummary analyzerParseExceptionSummaryFromWinDbg(const QString &text);

#endif // ANALYZER_COMMON_H
