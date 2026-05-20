#include "crash_analyzer.h"
#include "analyzer_backend.h"
#include "analyzer_common.h"
#include "report_renderer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#include <winnt.h>
#endif

namespace {

QString html(const QString &value)
{
    return value.toHtmlEscaped();
}

QString hexValue(quint64 value, int minWidth = 0)
{
    return QStringLiteral("0x%1")
        .arg(QString::number(value, 16).toUpper().rightJustified(minWidth, QLatin1Char('0')));
}

QString basename(const QString &path)
{
    return QFileInfo(path).fileName();
}

QString stem(const QString &path)
{
    return QFileInfo(path).completeBaseName();
}

QString nonEmpty(const QString &value, const QString &fallback)
{
    return value.isEmpty() ? fallback : value;
}

using ProcessResult = ::ProcessResult;

QString decodeText(const QByteArray &data)
{
    return analyzerDecodeText(data);
}

QString readTextFile(const QString &path)
{
    return analyzerReadTextFile(path);
}

bool writeTextFile(const QString &path, const QString &text, QString *error)
{
    return analyzerWriteTextFile(path, text, error);
}

QString sanitizeFileName(QString value)
{
    return analyzerSanitizeFileName(value);
}

QString runtimeRoot()
{
    return analyzerRuntimeRoot();
}

QString toolPath(const QString &rootPath, const QString &toolName)
{
    return analyzerToolPath(rootPath, toolName);
}

ProcessResult runCommandScript(const QString &scriptPath,
                               const QStringList &arguments,
                               const QString &workingDirectory)
{
    return analyzerRunCommandScript(scriptPath, arguments, workingDirectory);
}

QString symbolModuleName(const QString &symbolPath, bool includeExeExtension)
{
    return analyzerSymbolModuleName(symbolPath, includeExeExtension);
}

QString inferExecutablePath(const QString &symbolPath)
{
    return analyzerInferExecutablePath(symbolPath);
}

QString extractFirstValue(const QString &text, const QString &key)
{
    return analyzerExtractFirstValue(text, key);
}

QString extractModuleOffset(const QString &text)
{
    return analyzerExtractModuleOffset(text);
}

using ParsedExceptionSummary = ::ParsedExceptionSummary;

QString normalizeExceptionCodeFromText(QString codeText)
{
    return analyzerNormalizeExceptionCodeFromText(codeText);
}

ParsedExceptionSummary parseExceptionSummaryFromWinDbg(const QString &text)
{
    return analyzerParseExceptionSummaryFromWinDbg(text);
}

struct SourceLocation
{
    QString path;
    int line = 0;

    bool isValid() const
    {
        return !path.isEmpty() && line > 0;
    }
};

SourceLocation parseSourceLocation(const QString &locationText)
{
    const QRegularExpression expression(QStringLiteral("^(.+):(\\d+)(?::\\d+)?$"));
    const QRegularExpressionMatch match = expression.match(locationText.trimmed());
    if (!match.hasMatch()) {
        return {};
    }

    const QString path = match.captured(1).trimmed();
    const int line = match.captured(2).toInt();
    if (path == QStringLiteral("??") || line <= 0) {
        return {};
    }

    return { path, line };
}

QString normalizePathForMatch(QString path)
{
    return QDir::cleanPath(path.replace(QLatin1Char('\\'), QLatin1Char('/'))).toLower();
}

QString mapSourcePathFromRoot(const QString &rawPath, const QString &sourceRootPath)
{
    // 目标：把 dump 中记录的“原始源码路径”映射到用户当前机器上的源码目录。
    // 常见场景：构建机路径与本机路径不同，例如 D:\build\... -> C:\workspace\...
    if (rawPath.isEmpty() || sourceRootPath.isEmpty() || !QFileInfo(sourceRootPath).isDir()) {
        return {};
    }

    const QFileInfo rawInfo(rawPath);
    const QString fileName = rawInfo.fileName();
    if (fileName.isEmpty()) {
        return {};
    }

    QDir sourceRoot(sourceRootPath);
    const QString direct = sourceRoot.filePath(fileName);
    if (QFileInfo::exists(direct)) {
        return QFileInfo(direct).absoluteFilePath();
    }

    QStringList parts = normalizePathForMatch(rawPath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    while (!parts.isEmpty() && parts.first().contains(QLatin1Char(':'))) {
        parts.removeFirst();
    }

    const int maxDepth = std::min(8, static_cast<int>(parts.size()));
    for (int depth = maxDepth; depth >= 2; --depth) {
        // 优先按“路径后缀匹配”查找（例如 src/module/file.cpp），
        // 一般比全量扫描更快，也能较好应对路径前缀变化。
        const QString relativeTail = parts.mid(parts.size() - depth).join(QLatin1Char('/'));
        const QString candidate = sourceRoot.filePath(relativeTail);
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    QStringList candidates;
    QDirIterator iterator(sourceRootPath,
                          QStringList() << fileName,
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        candidates.append(iterator.next());
    }

    if (candidates.size() == 1) {
        return QFileInfo(candidates.first()).absoluteFilePath();
    }

    if (candidates.size() > 1) {
        // 如果同名文件不止一个，则按“从路径尾部开始的连续匹配段数”打分选最优。
        const QString normalizedRaw = normalizePathForMatch(rawPath);
        QString bestPath;
        int bestScore = -1;
        for (const QString &candidate : candidates) {
            const QString normalizedCandidate = normalizePathForMatch(candidate);
            int score = 0;
            const QStringList candidateParts = normalizedCandidate.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            const QStringList rawParts = normalizedRaw.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            for (int i = 1; i <= std::min(candidateParts.size(), rawParts.size()); ++i) {
                if (candidateParts.at(candidateParts.size() - i) == rawParts.at(rawParts.size() - i)) {
                    ++score;
                } else {
                    break;
                }
            }
            if (score > bestScore) {
                bestScore = score;
                bestPath = candidate;
            }
        }
        if (!bestPath.isEmpty()) {
            return QFileInfo(bestPath).absoluteFilePath();
        }
    }

    return {};
}

QString braceCharactersOutsideCommentsAndStrings(const QString &line, bool *inBlockComment)
{
    QString braces;
    bool inString = false;
    bool inCharacter = false;
    bool escaped = false;

    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = (i + 1 < line.size()) ? line.at(i + 1) : QChar();

        if (*inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                *inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inString || inCharacter) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (inString && ch == QLatin1Char('"')) {
                inString = false;
            } else if (inCharacter && ch == QLatin1Char('\'')) {
                inCharacter = false;
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            break;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            *inBlockComment = true;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('\'')) {
            inCharacter = true;
            continue;
        }
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}')) {
            braces.append(ch);
        }
    }

    return braces;
}

QString signatureBeforeBrace(const QStringList &lines, int braceLine)
{
    const int braceIndex = lines.at(braceLine).indexOf(QLatin1Char('{'));
    QString signature = braceIndex >= 0 ? lines.at(braceLine).left(braceIndex).trimmed() : QString();

    for (int i = braceLine - 1; i >= 0 && i >= braceLine - 10; --i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty()) {
            if (!signature.isEmpty()) {
                break;
            }
            continue;
        }
        if (trimmed.endsWith(QLatin1Char(';')) || trimmed.endsWith(QLatin1Char('}'))) {
            break;
        }

        signature.prepend(trimmed + QLatin1Char(' '));
        if (trimmed.contains(QLatin1Char(')'))) {
            break;
        }
    }

    return signature.simplified();
}

bool looksLikeFunctionOpening(const QStringList &lines, int braceLine)
{
    const QString signature = signatureBeforeBrace(lines, braceLine);
    if (!signature.contains(QLatin1Char(')'))) {
        return false;
    }

    static const QRegularExpression controlStatement(
        QStringLiteral("\\b(if|for|while|switch|catch|else)\\s*(\\(|$)"));
    return !controlStatement.match(signature).hasMatch();
}

QVector<int> openBraceStackAtLine(const QStringList &lines, int targetLine)
{
    QVector<int> stack;
    bool inBlockComment = false;

    for (int i = 0; i <= targetLine && i < lines.size(); ++i) {
        const QString braces = braceCharactersOutsideCommentsAndStrings(lines.at(i), &inBlockComment);
        for (const QChar brace : braces) {
            if (brace == QLatin1Char('{')) {
                stack.append(i);
            } else if (brace == QLatin1Char('}') && !stack.isEmpty()) {
                stack.removeLast();
            }
        }
    }

    return stack;
}

int matchingCloseBraceLine(const QStringList &lines, int openLine)
{
    int depth = 0;
    bool inBlockComment = false;

    for (int i = openLine; i < lines.size(); ++i) {
        const QString braces = braceCharactersOutsideCommentsAndStrings(lines.at(i), &inBlockComment);
        for (const QChar brace : braces) {
            if (brace == QLatin1Char('{')) {
                ++depth;
            } else if (brace == QLatin1Char('}')) {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
    }

    return -1;
}

int functionStartLine(const QStringList &lines, int openLine)
{
    int startLine = openLine;
    for (int i = openLine - 1; i >= 0 && i >= openLine - 10; --i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty()) {
            if (startLine != openLine) {
                break;
            }
            continue;
        }
        if (trimmed.endsWith(QLatin1Char(';')) || trimmed.endsWith(QLatin1Char('}'))) {
            break;
        }
        startLine = i;
        if (trimmed.contains(QLatin1Char(')'))) {
            break;
        }
    }

    return startLine;
}

QPair<int, int> functionRangeForLine(const QStringList &lines, int crashLineIndex)
{
    // 从崩溃行往外推导所属函数范围：
    // 1) 基于括号栈找到最接近的函数体起点；
    // 2) 找到对应右花括号作为函数结束；
    // 3) 若无法严格识别，则退化为崩溃行附近窗口。
    const int lastLine = static_cast<int>(lines.size()) - 1;
    const QVector<int> openBraces = openBraceStackAtLine(lines, crashLineIndex);
    for (auto it = openBraces.crbegin(); it != openBraces.crend(); ++it) {
        const int openLine = *it;
        if (!looksLikeFunctionOpening(lines, openLine)) {
            continue;
        }

        const int closeLine = matchingCloseBraceLine(lines, openLine);
        return { functionStartLine(lines, openLine),
                 closeLine >= 0 ? closeLine : std::min(lastLine, crashLineIndex + 20) };
    }

    return { std::max(0, crashLineIndex - 8),
             std::min(lastLine, crashLineIndex + 8) };
}

QString sourceCodeHtml(const QStringList &lines, int startLine, int endLine, int crashLine)
{
    QString output;
    const int width = QString::number(endLine + 1).size();

    for (int i = startLine; i <= endLine && i < lines.size(); ++i) {
        const int lineNumber = i + 1;
        const QString cssClass = lineNumber == crashLine
            ? QStringLiteral("code-line crash-code-line")
            : QStringLiteral("code-line");
        output += QStringLiteral("<span class='%1'><span class='line-no'>%2</span>  %3</span>\n")
                      .arg(cssClass,
                           QString::number(lineNumber).rightJustified(width, QLatin1Char(' ')),
                           html(lines.at(i)));
    }

    return output;
}

void attachCrashSourceCode(CrashReport *report, const QString &sourceRootPath)
{
    // 将“文件:行号”定位扩展成可展示的函数代码片段（并高亮崩溃行）。
    const SourceLocation location = parseSourceLocation(report->crashLocation);
    if (!location.isValid()) {
        return;
    }

    QString resolvedPath = location.path;
    if (!QFileInfo::exists(resolvedPath)) {
        const QString mappedPath = mapSourcePathFromRoot(location.path, sourceRootPath);
        if (!mappedPath.isEmpty()) {
            resolvedPath = mappedPath;
        }
    }

    QFile sourceFile(resolvedPath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        report->warnings.append(QStringLiteral("已定位到源码位置，但无法读取源码文件：%1").arg(location.path));
        return;
    }

    QString sourceText = decodeText(sourceFile.readAll());
    sourceText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    sourceText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = sourceText.split(QLatin1Char('\n'));
    if (location.line > lines.size()) {
        report->warnings.append(QStringLiteral("源码行号超出文件范围：%1:%2").arg(location.path).arg(location.line));
        return;
    }

    const QPair<int, int> range = functionRangeForLine(lines, location.line - 1);
    report->crashSourcePath = resolvedPath;
    report->crashSourceLine = location.line;
    report->crashSourceStartLine = range.first + 1;
    report->crashSourceCodeHtml = sourceCodeHtml(lines, range.first, range.second, location.line);
}

CrashReport analyzeWithScripts(const QString &dumpPath,
                               const QString &symbolPath,
                               const QString &sourceRootPath)
{
    // MinGW/.dbg 链路：
    // 1) 先调用 WinDbg 脚本生成原始崩溃日志；
    // 2) 再调用解析脚本还原模块偏移、函数、源码位置。
    CrashReport report;
    report.dumpPath = QFileInfo(dumpPath).absoluteFilePath();
    report.symbolPath = QFileInfo(symbolPath).absoluteFilePath();
    report.sourceRootPath = sourceRootPath.trimmed().isEmpty()
        ? QString()
        : QFileInfo(sourceRootPath).absoluteFilePath();

    if (!QFileInfo::exists(dumpPath)) {
        report.errorMessage = QStringLiteral("dump 文件不存在。");
        return report;
    }
    if (!QFileInfo::exists(symbolPath)) {
        report.errorMessage = QStringLiteral("符号文件不存在。");
        return report;
    }

    const QString rootPath = runtimeRoot();
    const QString generateScript = toolPath(rootPath, QStringLiteral("generate_windbg_dump_report.cmd"));
    const QString resolveScript = toolPath(rootPath, QStringLiteral("resolve_mingw_windbg_stack.cmd"));

    if (!QFileInfo::exists(generateScript) || !QFileInfo::exists(resolveScript)) {
        report.errorMessage = QStringLiteral("未在可执行程序同级 tools 目录下找到 WinDbg/MinGW 解析脚本：%1")
                                  .arg(QDir(rootPath).filePath(QStringLiteral("tools")));
        return report;
    }

    QDir runtimeDirectory(rootPath);
    if (!runtimeDirectory.exists(QStringLiteral("reports"))
        && !runtimeDirectory.mkpath(QStringLiteral("reports"))) {
        report.errorMessage = QStringLiteral("无法创建 reports 输出目录。");
        return report;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString dumpBaseName = sanitizeFileName(QFileInfo(dumpPath).completeBaseName());
    const QString reportBaseName = QStringLiteral("%1_%2").arg(dumpBaseName, timestamp);
    const QString winDbgReportPath = runtimeDirectory.filePath(QStringLiteral("reports/%1.windbg.txt").arg(reportBaseName));
    const QString resolvedReportPath = runtimeDirectory.filePath(QStringLiteral("reports/%1.resolved.txt").arg(reportBaseName));
    const QString winDbgModuleName = symbolModuleName(symbolPath, false);
    const QString resolveModuleName = symbolModuleName(symbolPath, true);
    const QString executablePath = inferExecutablePath(symbolPath);
    const QString buildDirectory = QFileInfo(executablePath).absolutePath();

    QStringList generateArguments;
    generateArguments << QStringLiteral("-DumpPath") << QDir::toNativeSeparators(dumpPath)
                      << QStringLiteral("-OutputPath") << QDir::toNativeSeparators(winDbgReportPath)
                      << QStringLiteral("-ModuleName") << winDbgModuleName;

    const ProcessResult generateResult = runCommandScript(generateScript,
                                                          generateArguments,
                                                          rootPath);
    report.scriptStdout = generateResult.standardOutput;
    if (!generateResult.standardError.trimmed().isEmpty()) {
        report.scriptStdout += QLatin1Char('\n') + generateResult.standardError;
    }
    if (!generateResult.ok) {
        report.errorMessage = generateResult.errorMessage;
        if (!report.scriptStdout.trimmed().isEmpty()) {
            report.errorMessage += QStringLiteral("<br/>") + html(report.scriptStdout).replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
        }
        return report;
    }

    QStringList resolveArguments;
    // IncludeNonStackMatches: 除调用栈外，也尝试在日志中提取非栈上的地址线索，
    // 可提升部分异常场景下的定位成功率。
    resolveArguments << QStringLiteral("-InputPath") << QDir::toNativeSeparators(winDbgReportPath)
                     << QStringLiteral("-BuildDir") << QDir::toNativeSeparators(buildDirectory)
                     << QStringLiteral("-ExePath") << QDir::toNativeSeparators(executablePath)
                     << QStringLiteral("-ModuleName") << resolveModuleName
                     << QStringLiteral("-IncludeNonStackMatches");

    if (QFileInfo(symbolPath).suffix().compare(QStringLiteral("dbg"), Qt::CaseInsensitive) == 0) {
        resolveArguments << QStringLiteral("-DbgPath") << QDir::toNativeSeparators(symbolPath);
    }

    const ProcessResult resolveResult = runCommandScript(resolveScript,
                                                        resolveArguments,
                                                        rootPath);
    QString resolvedText = resolveResult.standardOutput;
    if (!resolveResult.standardError.trimmed().isEmpty()) {
        resolvedText += QLatin1Char('\n') + resolveResult.standardError;
    }

    QString writeError;
    if (!writeTextFile(resolvedReportPath, resolvedText, &writeError)) {
        report.warnings.append(QStringLiteral("解析结果文件写入失败：%1").arg(writeError));
    }

    report.winDbgReportPath = QFileInfo(winDbgReportPath).absoluteFilePath();
    report.resolvedReportPath = QFileInfo(resolvedReportPath).absoluteFilePath();
    report.winDbgReportText = readTextFile(winDbgReportPath);
    report.resolvedReportText = resolvedText;
    const ParsedExceptionSummary parsedException = parseExceptionSummaryFromWinDbg(report.winDbgReportText);
    if (!parsedException.code.isEmpty()) {
        report.exceptionCode = parsedException.code;
    }
    if (!parsedException.description.isEmpty()) {
        report.exceptionDescription = parsedException.description;
    }
    report.exceptionModule = resolveModuleName;
    report.crashFrameLine = extractFirstValue(resolvedText, QStringLiteral("Frame"));
    report.crashFunctionOffset = extractModuleOffset(report.crashFrameLine);
    report.crashFunction = extractFirstValue(resolvedText, QStringLiteral("Function"));
    report.crashLocation = extractFirstValue(resolvedText, QStringLiteral("Location"));
    attachCrashSourceCode(&report, sourceRootPath);

    if (!resolveResult.ok) {
        report.warnings.append(QStringLiteral("WinDbg 日志已生成，但 MinGW 栈解析脚本执行失败：%1").arg(resolveResult.errorMessage));
        if (QFileInfo(symbolPath).suffix().compare(QStringLiteral("pdb"), Qt::CaseInsensitive) == 0) {
            report.warnings.append(QStringLiteral("当前 MinGW 解析脚本依赖 addr2line，PDB 通常只能由 WinDbg 直接解析；请优先使用 .dbg 文件解析 MinGW 调用栈。"));
        }
    }

    if (report.crashFunction.isEmpty()) {
        report.warnings.append(QStringLiteral("未从解析结果中提取到崩溃函数，请查看下方 WinDbg/MinGW 日志。"));
    }

    report.ok = true;
    return report;
}

#ifdef Q_OS_WIN

struct DumpModule
{
    quint64 base = 0;
    quint32 size = 0;
    QString imagePath;
    QString moduleName;
    QString codeViewPdbPath;
};

struct MemoryRange
{
    quint64 start = 0;
    quint64 rva = 0;
    quint64 size = 0;
};

class MappedDump
{
public:
    ~MappedDump()
    {
        close();
    }

    bool open(const QString &path, QString *error)
    {
        close();

        file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            *error = QStringLiteral("无法打开 dump 文件：%1").arg(winError(GetLastError()));
            return false;
        }

        LARGE_INTEGER fileSize {};
        if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
            *error = QStringLiteral("dump 文件为空或无法读取大小。");
            return false;
        }
        size = static_cast<quint64>(fileSize.QuadPart);

        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) {
            *error = QStringLiteral("无法创建 dump 文件映射：%1").arg(winError(GetLastError()));
            return false;
        }

        data = static_cast<const uchar *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (!data) {
            *error = QStringLiteral("无法映射 dump 文件：%1").arg(winError(GetLastError()));
            return false;
        }

        return true;
    }

    const void *rvaToPtr(quint64 rva, quint64 byteCount = 1) const
    {
        if (!data || rva >= size || byteCount > size - rva) {
            return nullptr;
        }

        return data + rva;
    }

    QString readMinidumpString(RVA rva) const
    {
        const auto *dumpString = static_cast<const MINIDUMP_STRING *>(rvaToPtr(rva, sizeof(ULONG32)));
        if (!dumpString) {
            return {};
        }

        const quint64 bytes = dumpString->Length;
        const quint64 totalBytes = sizeof(ULONG32) + bytes;
        dumpString = static_cast<const MINIDUMP_STRING *>(rvaToPtr(rva, totalBytes));
        if (!dumpString) {
            return {};
        }

        return QString::fromWCharArray(dumpString->Buffer, static_cast<int>(bytes / sizeof(WCHAR)));
    }

    QString codeViewPdbPath(const MINIDUMP_LOCATION_DESCRIPTOR &location) const
    {
        if (!location.Rva || location.DataSize < 4) {
            return {};
        }

        const auto *record = static_cast<const char *>(rvaToPtr(location.Rva, location.DataSize));
        if (!record) {
            return {};
        }

        qsizetype offset = -1;
        if (location.DataSize >= 24 && std::memcmp(record, "RSDS", 4) == 0) {
            offset = 24;
        } else if (location.DataSize >= 16 && std::memcmp(record, "NB10", 4) == 0) {
            offset = 16;
        }

        if (offset < 0 || static_cast<ULONG32>(offset) >= location.DataSize) {
            return {};
        }

        const char *name = record + offset;
        const qsizetype maxLength = static_cast<qsizetype>(location.DataSize) - offset;
        qsizetype length = 0;
        while (length < maxLength && name[length] != '\0') {
            ++length;
        }

        return QString::fromLocal8Bit(name, static_cast<int>(length));
    }

    bool readStreams(CrashReport *report, MINIDUMP_EXCEPTION_STREAM **exceptionStream, QString *error)
    {
        ULONG streamSize = 0;
        PMINIDUMP_DIRECTORY directory = nullptr;

        if (!MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                    ExceptionStream,
                                    &directory,
                                    reinterpret_cast<PVOID *>(exceptionStream),
                                    &streamSize)
            || !*exceptionStream
            || streamSize < sizeof(MINIDUMP_EXCEPTION_STREAM)) {
            *error = QStringLiteral("dump 中没有异常流，无法定位崩溃点。");
            return false;
        }

        MINIDUMP_SYSTEM_INFO *systemInfo = nullptr;
        streamSize = 0;
        if (MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                   SystemInfoStream,
                                   &directory,
                                   reinterpret_cast<PVOID *>(&systemInfo),
                                   &streamSize)
            && systemInfo
            && streamSize >= sizeof(MINIDUMP_SYSTEM_INFO)) {
            processorArchitecture = systemInfo->ProcessorArchitecture;
        }

        readModules(report);
        readMemoryRanges();
        return true;
    }

    const DumpModule *moduleForAddress(quint64 address) const
    {
        for (const DumpModule &module : modules) {
            const quint64 end = module.base + module.size;
            if (address >= module.base && (module.size == 0 || address < end)) {
                return &module;
            }
        }

        return nullptr;
    }

    DWORD64 moduleBaseForAddress(DWORD64 address) const
    {
        if (const DumpModule *module = moduleForAddress(address)) {
            return module->base;
        }

        return 0;
    }

    BOOL readMemory(DWORD64 address, PVOID buffer, DWORD requestedBytes, LPDWORD bytesRead) const
    {
        DWORD copied = 0;
        auto *target = static_cast<uchar *>(buffer);

        while (copied < requestedBytes) {
            const quint64 current = address + copied;
            const MemoryRange *range = nullptr;
            for (const MemoryRange &candidate : memoryRanges) {
                if (current >= candidate.start && current < candidate.start + candidate.size) {
                    range = &candidate;
                    break;
                }
            }

            if (!range) {
                break;
            }

            const quint64 offset = current - range->start;
            const quint64 available = range->size - offset;
            const quint64 toCopy = std::min<quint64>(requestedBytes - copied, available);
            const void *source = rvaToPtr(range->rva + offset, toCopy);
            if (!source) {
                break;
            }

            std::memcpy(target + copied, source, static_cast<size_t>(toCopy));
            copied += static_cast<DWORD>(toCopy);
        }

        if (bytesRead) {
            *bytesRead = copied;
        }

        return copied > 0 ? TRUE : FALSE;
    }

    USHORT processorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
    QVector<DumpModule> modules;

private:
    static QString winError(DWORD code)
    {
        wchar_t *buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageW(flags,
                                            nullptr,
                                            code,
                                            0,
                                            reinterpret_cast<LPWSTR>(&buffer),
                                            0,
                                            nullptr);
        QString message = length > 0
            ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
            : QStringLiteral("错误码 %1").arg(code);
        if (buffer) {
            LocalFree(buffer);
        }
        return message;
    }

    void close()
    {
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (mapping) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        size = 0;
        modules.clear();
        memoryRanges.clear();
    }

    void readModules(CrashReport *report)
    {
        ULONG streamSize = 0;
        PMINIDUMP_DIRECTORY directory = nullptr;
        MINIDUMP_MODULE_LIST *moduleList = nullptr;
        if (!MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                    ModuleListStream,
                                    &directory,
                                    reinterpret_cast<PVOID *>(&moduleList),
                                    &streamSize)
            || !moduleList) {
            report->warnings.append(QStringLiteral("dump 中没有模块列表，符号解析能力会受限。"));
            return;
        }

        const quint64 headerSize = offsetof(MINIDUMP_MODULE_LIST, Modules);
        const quint64 needed = headerSize
            + static_cast<quint64>(moduleList->NumberOfModules) * sizeof(MINIDUMP_MODULE);
        if (streamSize < needed) {
            report->warnings.append(QStringLiteral("dump 模块列表不完整，已跳过模块加载。"));
            return;
        }

        modules.reserve(static_cast<int>(moduleList->NumberOfModules));
        for (ULONG32 i = 0; i < moduleList->NumberOfModules; ++i) {
            const MINIDUMP_MODULE &entry = moduleList->Modules[i];
            DumpModule module;
            module.base = entry.BaseOfImage;
            module.size = entry.SizeOfImage;
            module.imagePath = readMinidumpString(entry.ModuleNameRva);
            module.moduleName = basename(module.imagePath);
            module.codeViewPdbPath = codeViewPdbPath(entry.CvRecord);
            modules.append(module);
        }
    }

    void readMemoryRanges()
    {
        ULONG streamSize = 0;
        PMINIDUMP_DIRECTORY directory = nullptr;

        MINIDUMP_MEMORY64_LIST *memory64List = nullptr;
        if (MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                   Memory64ListStream,
                                   &directory,
                                   reinterpret_cast<PVOID *>(&memory64List),
                                   &streamSize)
            && memory64List) {
            const quint64 headerSize = offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges);
            const quint64 needed = headerSize
                + static_cast<quint64>(memory64List->NumberOfMemoryRanges)
                    * sizeof(MINIDUMP_MEMORY_DESCRIPTOR64);
            if (streamSize >= needed) {
                quint64 rva = memory64List->BaseRva;
                for (quint64 i = 0; i < memory64List->NumberOfMemoryRanges; ++i) {
                    const MINIDUMP_MEMORY_DESCRIPTOR64 &range = memory64List->MemoryRanges[i];
                    memoryRanges.append({ range.StartOfMemoryRange, rva, range.DataSize });
                    rva += range.DataSize;
                }
            }
        }

        MINIDUMP_MEMORY_LIST *memoryList = nullptr;
        streamSize = 0;
        if (MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                   MemoryListStream,
                                   &directory,
                                   reinterpret_cast<PVOID *>(&memoryList),
                                   &streamSize)
            && memoryList) {
            const quint64 headerSize = offsetof(MINIDUMP_MEMORY_LIST, MemoryRanges);
            const quint64 needed = headerSize
                + static_cast<quint64>(memoryList->NumberOfMemoryRanges)
                    * sizeof(MINIDUMP_MEMORY_DESCRIPTOR);
            if (streamSize >= needed) {
                for (ULONG32 i = 0; i < memoryList->NumberOfMemoryRanges; ++i) {
                    const MINIDUMP_MEMORY_DESCRIPTOR &range = memoryList->MemoryRanges[i];
                    memoryRanges.append({ range.StartOfMemoryRange,
                                          range.Memory.Rva,
                                          range.Memory.DataSize });
                }
            }
        }

        MINIDUMP_THREAD_LIST *threadList = nullptr;
        streamSize = 0;
        if (MiniDumpReadDumpStream(const_cast<uchar *>(data),
                                   ThreadListStream,
                                   &directory,
                                   reinterpret_cast<PVOID *>(&threadList),
                                   &streamSize)
            && threadList) {
            const quint64 headerSize = offsetof(MINIDUMP_THREAD_LIST, Threads);
            const quint64 needed = headerSize
                + static_cast<quint64>(threadList->NumberOfThreads) * sizeof(MINIDUMP_THREAD);
            if (streamSize >= needed) {
                for (ULONG32 i = 0; i < threadList->NumberOfThreads; ++i) {
                    const MINIDUMP_MEMORY_DESCRIPTOR &stack = threadList->Threads[i].Stack;
                    if (stack.Memory.Rva && stack.Memory.DataSize > 0) {
                        memoryRanges.append({ stack.StartOfMemoryRange,
                                              stack.Memory.Rva,
                                              stack.Memory.DataSize });
                    }
                }
            }
        }
    }

    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const uchar *data = nullptr;
    quint64 size = 0;
    QVector<MemoryRange> memoryRanges;
};

class SymbolSession
{
public:
    explicit SymbolSession(const QString &searchPath)
    {
        handle = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS
                      | SYMOPT_LOAD_LINES
                      | SYMOPT_UNDNAME
                      | SYMOPT_FAIL_CRITICAL_ERRORS
                      | SYMOPT_INCLUDE_32BIT_MODULES
                      | SYMOPT_EXACT_SYMBOLS);
        initialized = SymInitializeW(handle,
                                      reinterpret_cast<PCWSTR>(searchPath.utf16()),
                                      FALSE);
    }

    ~SymbolSession()
    {
        if (initialized) {
            SymCleanup(handle);
        }
    }

    bool isReady() const
    {
        return initialized;
    }

    HANDLE processHandle() const
    {
        return handle;
    }

private:
    HANDLE handle = nullptr;
    bool initialized = false;
};

thread_local const MappedDump *activeDump = nullptr;

BOOL CALLBACK readDumpMemory(HANDLE, DWORD64 address, PVOID buffer, DWORD size, LPDWORD bytesRead)
{
    return activeDump ? activeDump->readMemory(address, buffer, size, bytesRead) : FALSE;
}

PVOID CALLBACK functionTableAccess(HANDLE process, DWORD64 address)
{
    return SymFunctionTableAccess64(process, address);
}

DWORD64 CALLBACK getModuleBase(HANDLE, DWORD64 address)
{
    return activeDump ? activeDump->moduleBaseForAddress(address) : 0;
}

QString describeException(const MINIDUMP_EXCEPTION &exception)
{
    const ULONG32 code = exception.ExceptionCode;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: {
        QString operation = QStringLiteral("访问");
        if (exception.NumberParameters > 0) {
            const ULONG64 accessType = exception.ExceptionInformation[0];
            if (accessType == 0) {
                operation = QStringLiteral("读取");
            } else if (accessType == 1) {
                operation = QStringLiteral("写入");
            } else if (accessType == 8) {
                operation = QStringLiteral("执行");
            }
        }

        QString target;
        if (exception.NumberParameters > 1) {
            target = QStringLiteral("，目标地址 %1").arg(hexValue(exception.ExceptionInformation[1]));
        }
        return QStringLiteral("访问冲突：%1内存失败%2").arg(operation, target);
    }
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return QStringLiteral("数组越界");
    case EXCEPTION_BREAKPOINT:
        return QStringLiteral("断点异常");
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return QStringLiteral("数据未对齐");
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return QStringLiteral("除零异常");
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return QStringLiteral("非法指令");
    case EXCEPTION_IN_PAGE_ERROR:
        return QStringLiteral("页内错误");
    case EXCEPTION_STACK_OVERFLOW:
        return QStringLiteral("栈溢出");
    default:
        return QStringLiteral("未识别异常");
    }
}

QString symbolSearchPath(const QString &dumpPath, const QString &symbolPath)
{
    QStringList paths;
    paths.append(QFileInfo(symbolPath).absolutePath());
    paths.append(QFileInfo(dumpPath).absolutePath());
    paths.removeDuplicates();
    return paths.join(QLatin1Char(';'));
}

bool symbolLooksRelated(const DumpModule &module, const QString &symbolPath)
{
    const QString symbolStem = stem(symbolPath);
    const QString moduleStem = stem(module.moduleName);
    const QString pdbStem = stem(module.codeViewPdbPath);

    return symbolStem.compare(moduleStem, Qt::CaseInsensitive) == 0
        || (!pdbStem.isEmpty() && symbolStem.compare(pdbStem, Qt::CaseInsensitive) == 0);
}

DWORD64 loadSelectedSymbolForCrashModule(HANDLE process,
                                         const DumpModule &module,
                                         const QString &symbolPath,
                                         QStringList *warnings)
{
    const QString moduleName = nonEmpty(module.moduleName, stem(symbolPath));
    const DWORD64 loadedBase = SymLoadModuleExW(process,
                                                nullptr,
                                                reinterpret_cast<PCWSTR>(symbolPath.utf16()),
                                                reinterpret_cast<PCWSTR>(moduleName.utf16()),
                                                module.base,
                                                module.size,
                                                nullptr,
                                                0);
    if (loadedBase == 0) {
        const DWORD selectedSymbolError = GetLastError();
        const DWORD64 fallbackBase = SymLoadModuleExW(process,
                                                      nullptr,
                                                      reinterpret_cast<PCWSTR>(module.imagePath.utf16()),
                                                      reinterpret_cast<PCWSTR>(moduleName.utf16()),
                                                      module.base,
                                                      module.size,
                                                      nullptr,
                                                      0);
        if (fallbackBase == 0) {
            warnings->append(QStringLiteral("无法按崩溃模块基址加载所选 PDB/DBG，且 dump 中记录的模块也加载失败。DbgHelp 错误码：%1/%2")
                                 .arg(selectedSymbolError)
                                 .arg(GetLastError()));
            return module.base;
        }

        warnings->append(QStringLiteral("无法直接加载所选 PDB/DBG，已回退到 dump 模块路径并使用符号目录继续解析。DbgHelp 错误码：%1")
                             .arg(selectedSymbolError));
        return fallbackBase;
    }

    return loadedBase;
}

void loadDumpModules(const MappedDump &dump, HANDLE process, quint64 crashAddress)
{
    QSet<quint64> loadedBases;
    for (const DumpModule &module : dump.modules) {
        if (!module.base || loadedBases.contains(module.base)) {
            continue;
        }

        const bool isCrashModule = crashAddress >= module.base
            && (module.size == 0 || crashAddress < module.base + module.size);
        if (isCrashModule) {
            continue;
        }

        const QString imageName = module.imagePath;
        const QString moduleName = nonEmpty(module.moduleName, stem(imageName));

        SymLoadModuleExW(process,
                         nullptr,
                         reinterpret_cast<PCWSTR>(imageName.utf16()),
                         reinterpret_cast<PCWSTR>(moduleName.utf16()),
                         module.base,
                         module.size,
                         nullptr,
                         0);
        loadedBases.insert(module.base);
    }
}

CrashStackFrame resolveFrame(HANDLE process, const MappedDump &dump, quint64 address, bool isCrashFrame)
{
    CrashStackFrame frame;
    frame.address = address;
    frame.isCrashFrame = isCrashFrame;

    if (const DumpModule *module = dump.moduleForAddress(address)) {
        frame.moduleBase = module->base;
        frame.moduleOffset = address - module->base;
        frame.moduleName = module->moduleName;
    }

    alignas(SYMBOL_INFOW) uchar symbolBuffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR)] {};
    auto *symbol = reinterpret_cast<SYMBOL_INFOW *>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0;
    if (SymFromAddrW(process, address, &displacement, symbol)) {
        frame.functionName = QString::fromWCharArray(symbol->Name);
        if (displacement > 0) {
            frame.functionName += QStringLiteral("+%1").arg(hexValue(displacement));
        }
    }

    IMAGEHLP_MODULEW64 moduleInfo {};
    moduleInfo.SizeOfStruct = sizeof(moduleInfo);
    if (SymGetModuleInfoW64(process, address, &moduleInfo) && moduleInfo.ModuleName[0] != L'\0') {
        frame.moduleName = QString::fromWCharArray(moduleInfo.ModuleName);
    }

    IMAGEHLP_LINEW64 line {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddrW64(process, address, &lineDisplacement, &line) && line.FileName) {
        frame.sourceFile = QString::fromWCharArray(line.FileName);
        frame.line = static_cast<int>(line.LineNumber);
    }

    return frame;
}

void appendFrameIfNew(QVector<CrashStackFrame> *frames, const CrashStackFrame &frame)
{
    if (frame.address == 0) {
        return;
    }

    for (const CrashStackFrame &existing : *frames) {
        if (existing.address == frame.address) {
            return;
        }
    }

    frames->append(frame);
}

bool copyContext(const MappedDump &dump,
                 const MINIDUMP_EXCEPTION_STREAM *exceptionStream,
                 void *context,
                 quint64 contextSize)
{
    const MINIDUMP_LOCATION_DESCRIPTOR &location = exceptionStream->ThreadContext;
    if (!location.Rva || location.DataSize == 0) {
        return false;
    }

    const quint64 bytes = std::min<quint64>(contextSize, location.DataSize);
    const void *source = dump.rvaToPtr(location.Rva, bytes);
    if (!source) {
        return false;
    }

    std::memset(context, 0, static_cast<size_t>(contextSize));
    std::memcpy(context, source, static_cast<size_t>(bytes));
    return true;
}

void walkStack(const MappedDump &dump,
               HANDLE process,
               const MINIDUMP_EXCEPTION_STREAM *exceptionStream,
               QVector<CrashStackFrame> *frames,
               QStringList *warnings)
{
    STACKFRAME64 stackFrame {};
    DWORD machineType = 0;
    void *contextRecord = nullptr;

#if defined(_M_X64) || defined(__x86_64__)
    CONTEXT amd64Context {};
    WOW64_CONTEXT x86Context {};

    if (dump.processorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        if (!copyContext(dump, exceptionStream, &amd64Context, sizeof(amd64Context))) {
            warnings->append(QStringLiteral("异常线程上下文缺失，无法展开调用栈。"));
            return;
        }

        machineType = IMAGE_FILE_MACHINE_AMD64;
        contextRecord = &amd64Context;
        stackFrame.AddrPC.Offset = amd64Context.Rip;
        stackFrame.AddrFrame.Offset = amd64Context.Rbp;
        stackFrame.AddrStack.Offset = amd64Context.Rsp;
    } else if (dump.processorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        if (!copyContext(dump, exceptionStream, &x86Context, sizeof(x86Context))) {
            warnings->append(QStringLiteral("异常线程上下文缺失，无法展开调用栈。"));
            return;
        }

        machineType = IMAGE_FILE_MACHINE_I386;
        contextRecord = &x86Context;
        stackFrame.AddrPC.Offset = x86Context.Eip;
        stackFrame.AddrFrame.Offset = x86Context.Ebp;
        stackFrame.AddrStack.Offset = x86Context.Esp;
    } else {
        warnings->append(QStringLiteral("当前仅支持 x86/x64 dump 的调用栈展开。"));
        return;
    }
#elif defined(_M_IX86) || defined(__i386__)
    CONTEXT x86Context {};
    if (dump.processorArchitecture != PROCESSOR_ARCHITECTURE_INTEL) {
        warnings->append(QStringLiteral("32 位构建当前只能展开 x86 dump 调用栈。"));
        return;
    }
    if (!copyContext(dump, exceptionStream, &x86Context, sizeof(x86Context))) {
        warnings->append(QStringLiteral("异常线程上下文缺失，无法展开调用栈。"));
        return;
    }
    machineType = IMAGE_FILE_MACHINE_I386;
    contextRecord = &x86Context;
    stackFrame.AddrPC.Offset = x86Context.Eip;
    stackFrame.AddrFrame.Offset = x86Context.Ebp;
    stackFrame.AddrStack.Offset = x86Context.Esp;
#else
    warnings->append(QStringLiteral("当前 CPU 架构暂不支持调用栈展开。"));
    return;
#endif

    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    activeDump = &dump;
    for (int i = 0; i < 64; ++i) {
        const BOOL walked = StackWalk64(machineType,
                                        process,
                                        nullptr,
                                        &stackFrame,
                                        contextRecord,
                                        readDumpMemory,
                                        functionTableAccess,
                                        getModuleBase,
                                        nullptr);
        if (!walked || stackFrame.AddrPC.Offset == 0) {
            break;
        }

        appendFrameIfNew(frames,
                         resolveFrame(process, dump, stackFrame.AddrPC.Offset, false));
    }
    activeDump = nullptr;

    if (frames->size() <= 1) {
        warnings->append(QStringLiteral("调用栈展开较少。若 dump 不包含足够内存或缺少对应模块镜像，可能只能解析崩溃地址。"));
    }
}

#endif // Q_OS_WIN

} // namespace

QString CrashReport::toHtml() const
{
    // 兼容旧调用：渲染逻辑已迁移到 ReportRenderer，避免模型层承担视图职责。
    return ReportRenderer::renderHtml(*this);
#if 0
    const ReportColors colors = reportColors();
    QString output;
    output += QStringLiteral("<html><head><style>"
                             "body{font-family:'Microsoft YaHei','Segoe UI',sans-serif;font-size:13px;"
                             "background:%1;color:%2;}"
                             "h2{font-size:20px;margin:0 0 12px 0;color:%2;}"
                             "h3{font-size:15px;margin:18px 0 8px 0;color:%2;}"
                             "table{border-collapse:collapse;width:100%;}"
                             "th,td{border-bottom:1px solid %3;padding:6px 8px;text-align:left;vertical-align:top;}"
                             "th{background:%4;color:%2;font-weight:600;}"
                             ".mono{font-family:Consolas,'Cascadia Mono',monospace;}"
                             ".crash{color:%5;font-weight:700;}"
                             ".crash-code-line{background:%6;color:%2;font-weight:700;}"
                             ".error{color:%5;font-weight:700;}"
                             ".muted{color:%7;}"
                             ".warn{color:%8;}"
                             ".source-code{white-space:pre;margin:0;padding:8px;border:1px solid %3;background:%4;}"
                             ".code-line{white-space:pre;}"
                             ".line-no{color:%7;}"
                             "</style></head><body>")
                  .arg(colors.background,
                       colors.text,
                       colors.border,
                       colors.headerBackground,
                       colors.crash,
                       colors.crashBackground,
                       colors.muted,
                       colors.warning);

    output += QStringLiteral("<h2>崩溃摘要报告</h2>");

    if (!ok) {
        output += QStringLiteral("<p class='error'>%1</p>").arg(html(errorMessage));
        output += QStringLiteral("</body></html>");
        return output;
    }

    if (!winDbgReportPath.isEmpty() || !resolvedReportPath.isEmpty()) {
        output += QStringLiteral("<table>");
        output += QStringLiteral("<tr><th>Dump</th><td class='mono'>%1</td></tr>").arg(html(dumpPath));
        output += QStringLiteral("<tr><th>符号文件</th><td class='mono'>%1</td></tr>").arg(html(symbolPath));
        if (!sourceRootPath.isEmpty()) {
            output += QStringLiteral("<tr><th>源码目录</th><td class='mono'>%1</td></tr>").arg(html(sourceRootPath));
        }
        output += QStringLiteral("<tr><th>WinDbg 日志</th><td class='mono'>%1</td></tr>").arg(html(winDbgReportPath));
        output += QStringLiteral("<tr><th>解析日志</th><td class='mono'>%1</td></tr>").arg(html(resolvedReportPath));
        output += QStringLiteral("<tr><th>目标模块</th><td>%1</td></tr>")
                      .arg(html(nonEmpty(exceptionModule, QStringLiteral("未解析"))));
        output += QStringLiteral("<tr><th>崩溃函数</th><td class='crash'>%1</td></tr>")
                      .arg(html(nonEmpty(crashFunction, QStringLiteral("未解析到函数名"))));
        output += QStringLiteral("<tr><th>崩溃偏移地址</th><td class='mono crash'>%1</td></tr>")
                      .arg(html(nonEmpty(crashFunctionOffset, QStringLiteral("-"))));
        output += QStringLiteral("<tr><th>源码位置</th><td class='mono crash'>%1</td></tr>")
                      .arg(html(nonEmpty(crashLocation, QStringLiteral("-"))));
        output += QStringLiteral("</table>");

        if (!warnings.isEmpty()) {
            output += QStringLiteral("<h3>提示</h3><ul>");
            for (const QString &warning : warnings) {
                output += QStringLiteral("<li class='warn'>%1</li>").arg(html(warning));
            }
            output += QStringLiteral("</ul>");
        }

        if (!crashSourceCodeHtml.isEmpty()) {
            output += QStringLiteral("<h3>崩溃函数代码</h3>");
            output += QStringLiteral("<p class='mono muted'>%1:%2</p>")
                          .arg(html(crashSourcePath))
                          .arg(crashSourceLine);
            output += QStringLiteral("<pre class='source-code mono'>%1</pre>")
                          .arg(crashSourceCodeHtml);
        }

        if (!resolvedReportText.trimmed().isEmpty()) {
            output += QStringLiteral("<h3>MinGW 解析结果</h3>");
            output += QStringLiteral("<pre class='mono' style='white-space:pre-wrap;margin:0;'>%1</pre>")
                          .arg(highlightedLogHtml(resolvedReportText));
        }

        if (!winDbgReportText.trimmed().isEmpty()) {
            output += QStringLiteral("<h3>WinDbg 崩溃日志</h3>");
            output += QStringLiteral("<pre class='mono' style='white-space:pre-wrap;margin:0;'>%1</pre>")
                          .arg(html(winDbgReportText));
        }

        output += QStringLiteral("</body></html>");
        return output;
    }

    output += QStringLiteral("<table>");
    output += QStringLiteral("<tr><th>Dump</th><td class='mono'>%1</td></tr>").arg(html(dumpPath));
    output += QStringLiteral("<tr><th>符号文件</th><td class='mono'>%1</td></tr>").arg(html(symbolPath));
    output += QStringLiteral("<tr><th>异常类型</th><td>%1 <span class='mono muted'>(%2)</span></td></tr>")
                  .arg(html(exceptionDescription), html(exceptionCode));
    output += QStringLiteral("<tr><th>异常地址</th><td class='mono'>%1</td></tr>")
                  .arg(html(hexValue(exceptionAddress)));
    output += QStringLiteral("<tr><th>崩溃模块</th><td>%1</td></tr>")
                  .arg(html(nonEmpty(exceptionModule, QStringLiteral("未解析"))));
    output += QStringLiteral("<tr><th>模块基址</th><td class='mono'>%1</td></tr>")
                  .arg(crashModuleBase ? html(hexValue(crashModuleBase)) : QStringLiteral("-"));
    output += QStringLiteral("<tr><th>崩溃偏移</th><td class='mono crash'>%1</td></tr>")
                  .arg(crashModuleBase ? html(hexValue(crashAddressOffset)) : QStringLiteral("-"));
    output += QStringLiteral("<tr><th>崩溃线程</th><td class='mono'>%1</td></tr>").arg(crashThreadId);
    output += QStringLiteral("<tr><th>崩溃函数</th><td class='crash'>%1</td></tr>")
                  .arg(html(nonEmpty(crashFunction, QStringLiteral("未解析到函数名"))));
    output += QStringLiteral("</table>");

    if (!warnings.isEmpty()) {
        output += QStringLiteral("<h3>提示</h3><ul>");
        for (const QString &warning : warnings) {
            output += QStringLiteral("<li class='warn'>%1</li>").arg(html(warning));
        }
        output += QStringLiteral("</ul>");
    }

    output += QStringLiteral("<h3>异常线程调用栈</h3>");
    output += QStringLiteral("<table><tr><th>#</th><th>地址</th><th>模块偏移</th><th>模块</th><th>函数</th><th>源码</th></tr>");
    for (qsizetype i = 0; i < frames.size(); ++i) {
        const CrashStackFrame &frame = frames.at(i);
        const QString rowClass = frame.isCrashFrame ? QStringLiteral(" class='crash'") : QString();
        const QString offset = frame.moduleBase ? hexValue(frame.moduleOffset) : QStringLiteral("");
        const QString source = frame.sourceFile.isEmpty()
            ? QStringLiteral("")
            : QStringLiteral("%1:%2").arg(frame.sourceFile).arg(frame.line);
        output += QStringLiteral("<tr%1><td>%2</td><td class='mono'>%3</td><td class='mono'>%4</td><td>%5</td><td>%6</td><td class='mono'>%7</td></tr>")
                      .arg(rowClass,
                           QString::number(i),
                           html(hexValue(frame.address)),
                           html(offset),
                           html(nonEmpty(frame.moduleName, QStringLiteral("-"))),
                           html(nonEmpty(frame.functionName, QStringLiteral("未解析"))),
                           html(source));
    }
    output += QStringLiteral("</table>");
    output += QStringLiteral("</body></html>");
    return output;
#endif
}

CrashReport analyzePdbDumpWithDbgHelp(const QString &dumpPath,
                                      const QString &symbolPath,
                                      const QString &sourceRootPath)
{
    // PDB 链路：直接用 DbgHelp 读取 dump + 符号，做结构化调用栈解析。
    CrashReport report;
    report.dumpPath = QFileInfo(dumpPath).absoluteFilePath();
    report.symbolPath = QFileInfo(symbolPath).absoluteFilePath();
    report.sourceRootPath = sourceRootPath.trimmed().isEmpty()
        ? QString()
        : QFileInfo(sourceRootPath).absoluteFilePath();

    if (!QFileInfo::exists(dumpPath)) {
        report.errorMessage = QStringLiteral("dump 文件不存在。");
        return report;
    }
    if (!QFileInfo::exists(symbolPath)) {
        report.errorMessage = QStringLiteral("符号文件不存在。");
        return report;
    }

#ifndef Q_OS_WIN
    report.errorMessage = QStringLiteral("当前实现依赖 Windows DbgHelp，只能在 Windows 上解析 dump。");
    return report;
#else
    MappedDump dump;
    QString error;
    if (!dump.open(dumpPath, &error)) {
        report.errorMessage = error;
        return report;
    }

    MINIDUMP_EXCEPTION_STREAM *exceptionStream = nullptr;
    if (!dump.readStreams(&report, &exceptionStream, &error)) {
        report.errorMessage = error;
        return report;
    }

    const MINIDUMP_EXCEPTION &exception = exceptionStream->ExceptionRecord;
    report.exceptionCode = hexValue(exception.ExceptionCode, 8);
    report.exceptionDescription = describeException(exception);
    report.exceptionAddress = exception.ExceptionAddress;
    report.crashThreadId = exceptionStream->ThreadId;

    const DumpModule *crashModule = dump.moduleForAddress(report.exceptionAddress);
    if (crashModule) {
        // 先锁定崩溃模块与模块内偏移，后续函数解析和“崩溃偏移地址”都基于它。
        report.exceptionModule = crashModule->moduleName;
        report.crashModuleBase = crashModule->base;
        report.crashAddressOffset = report.exceptionAddress - crashModule->base;
        if (!symbolLooksRelated(*crashModule, symbolPath)) {
            report.warnings.append(QStringLiteral("所选 PDB/DBG 文件名与 dump 中记录的崩溃模块不完全一致，已按崩溃偏移尝试解析。"));
        }
    } else {
        report.warnings.append(QStringLiteral("dump 模块列表中没有找到异常地址所属模块，无法计算模块内偏移。"));
    }

    SymbolSession symbols(symbolSearchPath(dumpPath, symbolPath));
    if (!symbols.isReady()) {
        report.errorMessage = QStringLiteral("DbgHelp 符号会话初始化失败。");
        return report;
    }

    DWORD64 crashSymbolBase = report.crashModuleBase;
    if (crashModule) {
        // 优先把用户指定的符号文件绑定到崩溃模块，减少符号串扰。
        crashSymbolBase = loadSelectedSymbolForCrashModule(symbols.processHandle(),
                                                           *crashModule,
                                                           symbolPath,
                                                           &report.warnings);
    }
    loadDumpModules(dump, symbols.processHandle(), report.exceptionAddress);

    const quint64 crashLookupAddress = crashModule
        ? crashSymbolBase + report.crashAddressOffset
        : report.exceptionAddress;
    // 先解析崩溃点，再展开调用栈，确保摘要区总能优先展示“最关键帧”。
    CrashStackFrame crashFrame = resolveFrame(symbols.processHandle(), dump, crashLookupAddress, true);
    crashFrame.address = report.exceptionAddress;
    crashFrame.moduleBase = report.crashModuleBase;
    crashFrame.moduleOffset = report.crashAddressOffset;
    report.crashFunction = crashFrame.functionName;
    report.crashFunctionOffset = hexValue(report.crashAddressOffset);
    if (!crashFrame.sourceFile.isEmpty() && crashFrame.line > 0) {
        report.crashLocation = QStringLiteral("%1:%2").arg(crashFrame.sourceFile).arg(crashFrame.line);
        attachCrashSourceCode(&report, sourceRootPath);
    }
    if (!crashFrame.moduleName.isEmpty()) {
        report.exceptionModule = crashFrame.moduleName;
    }
    appendFrameIfNew(&report.frames, crashFrame);

    walkStack(dump,
              symbols.processHandle(),
              exceptionStream,
              &report.frames,
              &report.warnings);

    if (report.crashFunction.isEmpty()) {
        report.warnings.prepend(QStringLiteral("未解析到崩溃函数名，请确认选择的 PDB/DBG 与 dump 中崩溃模块完全匹配。"));
    }

    report.ok = true;
    return report;
#endif
}

CrashReport analyzeDbgDumpWithScripts(const QString &dumpPath,
                                      const QString &symbolPath,
                                      const QString &sourceRootPath)
{
    return analyzeWithScripts(dumpPath, symbolPath, sourceRootPath);
}
