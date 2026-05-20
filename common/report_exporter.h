#ifndef REPORT_EXPORTER_H
#define REPORT_EXPORTER_H

#include "crash_analyzer.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

struct ReportExportOptions
{
    QString outputDirectory;
    bool exportMarkdown = true;
    bool exportPdf = false;
    bool exportText = false;
};

struct ReportExportResult
{
    QStringList exportedFiles;
    QStringList errors;

    bool ok() const
    {
        return !exportedFiles.isEmpty();
    }
};

class ReportExporter
{
public:
    static QString defaultOutputDirectory();
    static QString buildExportBaseName(const CrashReport &report,
                                       const QDateTime &now = QDateTime::currentDateTime());
    static ReportExportResult exportReport(const CrashReport &report,
                                           const ReportExportOptions &options);
};

#endif // REPORT_EXPORTER_H
