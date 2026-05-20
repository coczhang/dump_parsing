#include "analyzer_common.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString removeTrailingExtension(QString value, const QString &extension)
{
    if (value.endsWith(extension, Qt::CaseInsensitive)) {
        value.chop(extension.size());
    }

    return value;
}

} // namespace

QString analyzerDecodeText(const QByteArray &data)
{
    if (data.startsWith("\xFF\xFE")) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + 2),
                                  (data.size() - 2) / 2);
    }

    if (data.startsWith("\xFE\xFF")) {
        QString text;
        text.reserve((data.size() - 2) / 2);
        for (qsizetype i = 2; i + 1 < data.size(); i += 2) {
            const ushort value = static_cast<ushort>((static_cast<uchar>(data.at(i)) << 8)
                                                     | static_cast<uchar>(data.at(i + 1)));
            text.append(QChar(value));
        }
        return text;
    }

    const QString utf8Text = QString::fromUtf8(data);
    if (!utf8Text.contains(QChar::ReplacementCharacter)) {
        return utf8Text;
    }

    return QString::fromLocal8Bit(data);
}

QString analyzerReadTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return analyzerDecodeText(file.readAll());
}

bool analyzerWriteTextFile(const QString &path, const QString &text, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    file.write(text.toUtf8());
    return true;
}

QString analyzerSanitizeFileName(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    value = value.trimmed();
    return value.isEmpty() ? QStringLiteral("dump") : value;
}

QString analyzerRuntimeRoot()
{
    return QCoreApplication::applicationDirPath();
}

QString analyzerToolPath(const QString &rootPath, const QString &toolName)
{
    return QDir(rootPath).filePath(QStringLiteral("tools/%1").arg(toolName));
}

ProcessResult analyzerRunCommandScript(const QString &scriptPath,
                                       const QStringList &arguments,
                                       const QString &workingDirectory)
{
    ProcessResult result;

    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    QStringList commandArguments;
    commandArguments << QStringLiteral("/C") << QDir::toNativeSeparators(scriptPath);
    commandArguments << arguments;
    process.setArguments(commandArguments);
    process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted()) {
        result.errorMessage = QStringLiteral("无法启动脚本：%1").arg(process.errorString());
        return result;
    }

    process.waitForFinished(-1);
    result.exitCode = process.exitCode();
    result.standardOutput = analyzerDecodeText(process.readAllStandardOutput());
    result.standardError = analyzerDecodeText(process.readAllStandardError());
    result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;

    if (!result.ok) {
        result.errorMessage = QStringLiteral("脚本执行失败：%1，退出码 %2")
                                  .arg(QFileInfo(scriptPath).fileName())
                                  .arg(result.exitCode);
    }

    return result;
}

QString analyzerSymbolModuleName(const QString &symbolPath, bool includeExeExtension)
{
    QString fileName = QFileInfo(symbolPath).fileName();
    fileName = removeTrailingExtension(fileName, QStringLiteral(".dbg"));
    fileName = removeTrailingExtension(fileName, QStringLiteral(".pdb"));

    if (includeExeExtension) {
        if (!fileName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            fileName += QStringLiteral(".exe");
        }
        return fileName;
    }

    fileName = removeTrailingExtension(fileName, QStringLiteral(".exe"));
    return fileName;
}

QString analyzerInferExecutablePath(const QString &symbolPath)
{
    QFileInfo symbolInfo(symbolPath);
    QString fileName = symbolInfo.fileName();

    if (fileName.endsWith(QStringLiteral(".exe.dbg"), Qt::CaseInsensitive)) {
        QString executablePath = symbolInfo.absoluteFilePath();
        executablePath.chop(QStringLiteral(".dbg").size());
        if (QFileInfo::exists(executablePath)) {
            return executablePath;
        }
    }

    QString moduleName = analyzerSymbolModuleName(symbolPath, false) + QStringLiteral(".exe");
    const QString siblingExecutable = QDir(symbolInfo.absolutePath()).filePath(moduleName);
    if (QFileInfo::exists(siblingExecutable)) {
        return siblingExecutable;
    }

    return QCoreApplication::applicationFilePath();
}

QString analyzerExtractFirstValue(const QString &text, const QString &key)
{
    const QRegularExpression expression(QStringLiteral("(?m)^%1\\s*:\\s*(.+)$")
                                            .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString analyzerExtractModuleOffset(const QString &text)
{
    const QRegularExpression expression(QStringLiteral("\\b([A-Za-z0-9_.-]+(?:\\.exe)?\\+0x[0-9A-Fa-f]+)\\b"));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString analyzerNormalizeExceptionCodeFromText(QString codeText)
{
    codeText = codeText.trimmed();
    if (codeText.isEmpty()) {
        return {};
    }

    bool ok = false;
    QString hexPart = codeText;
    if (hexPart.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexPart = hexPart.mid(2);
    }

    const quint64 value = hexPart.toULongLong(&ok, 16);
    if (!ok) {
        return codeText.toUpper();
    }

    return QStringLiteral("0x%1")
        .arg(QString::number(value, 16).toUpper().rightJustified(hexPart.size() <= 8 ? 8 : 0, QLatin1Char('0')));
}

ParsedExceptionSummary analyzerParseExceptionSummaryFromWinDbg(const QString &text)
{
    ParsedExceptionSummary summary;

    static const QRegularExpression codeRegex(
        QStringLiteral("(?im)^EXCEPTION_CODE\\s*:\\s*(?:\\([^\\)]*\\)\\s*)?"
                       "(0x[0-9A-Fa-f]+|[0-9A-Fa-f]{8,16})(?:\\s*-\\s*(.+))?$"));
    const QRegularExpressionMatch codeMatch = codeRegex.match(text);
    if (codeMatch.hasMatch()) {
        summary.code = analyzerNormalizeExceptionCodeFromText(codeMatch.captured(1));
        summary.description = codeMatch.captured(2).trimmed();
    }

    if (summary.code.isEmpty()) {
        static const QRegularExpression errorCodeRegex(
            QStringLiteral("(?im)^ERROR_CODE\\s*:\\s*(?:\\([^\\)]*\\)\\s*)?"
                           "(0x[0-9A-Fa-f]+|[0-9A-Fa-f]{8,16})(?:\\s*-\\s*(.+))?$"));
        const QRegularExpressionMatch errorCodeMatch = errorCodeRegex.match(text);
        if (errorCodeMatch.hasMatch()) {
            summary.code = analyzerNormalizeExceptionCodeFromText(errorCodeMatch.captured(1));
            if (summary.description.isEmpty()) {
                summary.description = errorCodeMatch.captured(2).trimmed();
            }
        }
    }

    if (summary.code.isEmpty()) {
        static const QRegularExpression codeStrRegex(
            QStringLiteral("(?im)^EXCEPTION_CODE_STR\\s*:\\s*([0-9A-Fa-f]{8,16})\\s*$"));
        const QRegularExpressionMatch codeStrMatch = codeStrRegex.match(text);
        if (codeStrMatch.hasMatch()) {
            summary.code = analyzerNormalizeExceptionCodeFromText(codeStrMatch.captured(1));
        }
    }

    if (summary.description.isEmpty()) {
        static const QRegularExpression exceptionStrRegex(
            QStringLiteral("(?im)^EXCEPTION_STR\\s*:\\s*(.+)$"));
        const QRegularExpressionMatch exceptionStrMatch = exceptionStrRegex.match(text);
        if (exceptionStrMatch.hasMatch()) {
            summary.description = exceptionStrMatch.captured(1).trimmed();
        }
    }

    return summary;
}
