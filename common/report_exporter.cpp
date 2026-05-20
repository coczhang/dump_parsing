#include "report_exporter.h"
#include "report_renderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPrinter>
#include <QStringConverter>
#include <QTextDocument>
#include <QTextStream>

namespace {

bool writeTextFile(const QString &path, const QString &content, QString *error)
{
    // 统一按 UTF-8 输出，确保中文路径/中文报告在不同编辑器中不乱码。
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        if (error) {
            *error = QStringLiteral("文本写入失败");
        }
        return false;
    }

    return true;
}

} // namespace

QString ReportExporter::defaultOutputDirectory()
{
    // 默认导出到可执行程序同级目录，便于打包后直接使用。
    return QDir(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath())
        .filePath(QStringLiteral("exports"));
}

QString ReportExporter::buildExportBaseName(const CrashReport &report, const QDateTime &now)
{
    QString baseName = QFileInfo(report.dumpPath).completeBaseName();
    if (baseName.trimmed().isEmpty()) {
        baseName = QStringLiteral("crash_report");
    }

    return QStringLiteral("%1_%2")
        .arg(baseName, now.toString(QStringLiteral("yyyyMMdd_HHmmss")));
}

ReportExportResult ReportExporter::exportReport(const CrashReport &report,
                                                const ReportExportOptions &options)
{
    ReportExportResult result;
    // 只允许导出“成功分析”的报告，避免导出空文档。
    if (!report.ok) {
        result.errors.append(QStringLiteral("当前报告没有可导出的分析结果。"));
        return result;
    }

    const bool hasFormat = options.exportMarkdown || options.exportPdf || options.exportText;
    if (!hasFormat) {
        result.errors.append(QStringLiteral("请至少选择一种导出格式。"));
        return result;
    }

    const QString outputPath = options.outputDirectory.trimmed().isEmpty()
        ? defaultOutputDirectory()
        : options.outputDirectory.trimmed();
    QDir outputDir(outputPath);
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        result.errors.append(QStringLiteral("无法创建导出目录：%1")
                                 .arg(QDir::toNativeSeparators(outputPath)));
        return result;
    }

    const QString baseName = buildExportBaseName(report);
    // 先渲染一次 HTML，再派生为 Markdown/TXT/PDF，
    // 保证多种格式的数据源一致，减少格式间内容偏差。
    QTextDocument document;
    document.setHtml(ReportRenderer::renderHtml(report));
    const QString plainText = document.toPlainText();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QString markdownText = document.toMarkdown();
    if (markdownText.trimmed().isEmpty()) {
        markdownText = plainText;
    }
#else
    const QString markdownText = plainText;
#endif

    if (options.exportMarkdown) {
        const QString markdownPath = outputDir.filePath(baseName + QStringLiteral(".md"));
        QString error;
        if (writeTextFile(markdownPath, markdownText, &error)) {
            result.exportedFiles.append(QDir::toNativeSeparators(markdownPath));
        } else {
            result.errors.append(QStringLiteral("Markdown 导出失败：%1").arg(error));
        }
    }

    if (options.exportText) {
        const QString textPath = outputDir.filePath(baseName + QStringLiteral(".txt"));
        QString error;
        if (writeTextFile(textPath, plainText, &error)) {
            result.exportedFiles.append(QDir::toNativeSeparators(textPath));
        } else {
            result.errors.append(QStringLiteral("TXT 导出失败：%1").arg(error));
        }
    }

    if (options.exportPdf) {
        const QString pdfPath = outputDir.filePath(baseName + QStringLiteral(".pdf"));
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(pdfPath);
        document.print(&printer);
        QFileInfo pdfInfo(pdfPath);
        if (pdfInfo.exists() && pdfInfo.size() > 0) {
            result.exportedFiles.append(QDir::toNativeSeparators(pdfPath));
        } else {
            result.errors.append(QStringLiteral("PDF 导出失败：无法写入 PDF 文件。"));
        }
    }

    return result;
}
