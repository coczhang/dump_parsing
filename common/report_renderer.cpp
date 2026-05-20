#include "report_renderer.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>

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

QString nonEmpty(const QString &value, const QString &fallback)
{
    return value.isEmpty() ? fallback : value;
}

QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

struct ReportColors
{
    QString background;
    QString text;
    QString headerBackground;
    QString border;
    QString crash;
    QString crashBackground;
    QString muted;
    QString warning;
};

ReportColors reportColors()
{
    // 基于系统调色板动态取色，保证浅色/深色主题下可读性一致。
    const QPalette palette = QGuiApplication::palette();
    const QColor background = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    const bool darkTheme = background.lightness() < text.lightness();

    QColor headerBackground = palette.color(QPalette::Button);
    if (headerBackground == background) {
        headerBackground = palette.color(QPalette::AlternateBase);
    }

    return {
        colorName(background),
        colorName(text),
        colorName(headerBackground),
        colorName(palette.color(QPalette::Mid)),
        darkTheme ? QStringLiteral("#ff6b6b") : QStringLiteral("#c62828"),
        darkTheme ? QStringLiteral("#4a1f1f") : QStringLiteral("#ffe8e8"),
        colorName(palette.color(QPalette::Disabled, QPalette::WindowText)),
        darkTheme ? QStringLiteral("#ffd166") : QStringLiteral("#8a5a00")
    };
}

QString highlightedLogHtml(const QString &text)
{
    // 对脚本输出中 “Likely Crash Site” 段落做高亮，
    // 便于用户快速定位最可能的崩溃点。
    QString output;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool inLikelyCrashSite = false;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("Likely Crash Site:")) {
            inLikelyCrashSite = true;
            output += QStringLiteral("<span class='crash'>%1</span>\n").arg(html(line));
            continue;
        }

        if (inLikelyCrashSite && trimmed.isEmpty()) {
            inLikelyCrashSite = false;
            output += QLatin1Char('\n');
            continue;
        }

        if (inLikelyCrashSite) {
            output += QStringLiteral("<span class='crash'>%1</span>\n").arg(html(line));
        } else {
            output += html(line) + QLatin1Char('\n');
        }
    }

    return output;
}

} // namespace

QString ReportRenderer::renderHtml(const CrashReport &report)
{
    // 单一入口：统一负责 CrashReport -> HTML，避免 UI/导出各自拼接导致样式分叉。
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

    if (!report.ok) {
        // 分析失败场景直接输出错误文本，避免后续字段为空时出现误导信息。
        output += QStringLiteral("<p class='error'>%1</p>").arg(html(report.errorMessage));
        output += QStringLiteral("</body></html>");
        return output;
    }

    // MinGW(.dbg) 脚本链路：以 WinDbg/解析日志作为主信息源展示。
    if (!report.winDbgReportPath.isEmpty() || !report.resolvedReportPath.isEmpty()) {
        output += QStringLiteral("<table>");
        output += QStringLiteral("<tr><th>Dump</th><td class='mono'>%1</td></tr>").arg(html(report.dumpPath));
        output += QStringLiteral("<tr><th>符号文件</th><td class='mono'>%1</td></tr>").arg(html(report.symbolPath));
        if (!report.sourceRootPath.isEmpty()) {
            output += QStringLiteral("<tr><th>源码目录</th><td class='mono'>%1</td></tr>").arg(html(report.sourceRootPath));
        }
        output += QStringLiteral("<tr><th>WinDbg 日志</th><td class='mono'>%1</td></tr>").arg(html(report.winDbgReportPath));
        output += QStringLiteral("<tr><th>解析日志</th><td class='mono'>%1</td></tr>").arg(html(report.resolvedReportPath));
        output += QStringLiteral("<tr><th>目标模块</th><td>%1</td></tr>")
                      .arg(html(nonEmpty(report.exceptionModule, QStringLiteral("未解析"))));
        output += QStringLiteral("<tr><th>崩溃函数</th><td class='crash'>%1</td></tr>")
                      .arg(html(nonEmpty(report.crashFunction, QStringLiteral("未解析到函数名"))));
        output += QStringLiteral("<tr><th>崩溃偏移地址</th><td class='mono crash'>%1</td></tr>")
                      .arg(html(nonEmpty(report.crashFunctionOffset, QStringLiteral("-"))));
        output += QStringLiteral("<tr><th>源码位置</th><td class='mono crash'>%1</td></tr>")
                      .arg(html(nonEmpty(report.crashLocation, QStringLiteral("-"))));
        output += QStringLiteral("</table>");

        if (!report.warnings.isEmpty()) {
            output += QStringLiteral("<h3>提示</h3><ul>");
            for (const QString &warning : report.warnings) {
                output += QStringLiteral("<li class='warn'>%1</li>").arg(html(warning));
            }
            output += QStringLiteral("</ul>");
        }

        if (!report.crashSourceCodeHtml.isEmpty()) {
            output += QStringLiteral("<h3>崩溃函数代码</h3>");
            output += QStringLiteral("<p class='mono muted'>%1:%2</p>")
                          .arg(html(report.crashSourcePath))
                          .arg(report.crashSourceLine);
            output += QStringLiteral("<pre class='source-code mono'>%1</pre>")
                          .arg(report.crashSourceCodeHtml);
        }

        if (!report.resolvedReportText.trimmed().isEmpty()) {
            output += QStringLiteral("<h3>MinGW 解析结果</h3>");
            output += QStringLiteral("<pre class='mono' style='white-space:pre-wrap;margin:0;'>%1</pre>")
                          .arg(highlightedLogHtml(report.resolvedReportText));
        }

        if (!report.winDbgReportText.trimmed().isEmpty()) {
            output += QStringLiteral("<h3>WinDbg 崩溃日志</h3>");
            output += QStringLiteral("<pre class='mono' style='white-space:pre-wrap;margin:0;'>%1</pre>")
                          .arg(html(report.winDbgReportText));
        }

        output += QStringLiteral("</body></html>");
        return output;
    }

    // PDB/DbgHelp 链路：以异常码、地址和调用栈结构化展示为主。
    output += QStringLiteral("<table>");
    output += QStringLiteral("<tr><th>Dump</th><td class='mono'>%1</td></tr>").arg(html(report.dumpPath));
    output += QStringLiteral("<tr><th>符号文件</th><td class='mono'>%1</td></tr>").arg(html(report.symbolPath));
    output += QStringLiteral("<tr><th>异常类型</th><td>%1 <span class='mono muted'>(%2)</span></td></tr>")
                  .arg(html(report.exceptionDescription), html(report.exceptionCode));
    output += QStringLiteral("<tr><th>异常地址</th><td class='mono'>%1</td></tr>")
                  .arg(html(hexValue(report.exceptionAddress)));
    output += QStringLiteral("<tr><th>崩溃模块</th><td>%1</td></tr>")
                  .arg(html(nonEmpty(report.exceptionModule, QStringLiteral("未解析"))));
    output += QStringLiteral("<tr><th>模块基址</th><td class='mono'>%1</td></tr>")
                  .arg(report.crashModuleBase ? html(hexValue(report.crashModuleBase)) : QStringLiteral("-"));
    output += QStringLiteral("<tr><th>崩溃偏移</th><td class='mono crash'>%1</td></tr>")
                  .arg(report.crashModuleBase ? html(hexValue(report.crashAddressOffset)) : QStringLiteral("-"));
    output += QStringLiteral("<tr><th>崩溃线程</th><td class='mono'>%1</td></tr>").arg(report.crashThreadId);
    output += QStringLiteral("<tr><th>崩溃函数</th><td class='crash'>%1</td></tr>")
                  .arg(html(nonEmpty(report.crashFunction, QStringLiteral("未解析到函数名"))));
    output += QStringLiteral("</table>");

    if (!report.warnings.isEmpty()) {
        output += QStringLiteral("<h3>提示</h3><ul>");
        for (const QString &warning : report.warnings) {
            output += QStringLiteral("<li class='warn'>%1</li>").arg(html(warning));
        }
        output += QStringLiteral("</ul>");
    }

    output += QStringLiteral("<h3>异常线程调用栈</h3>");
    output += QStringLiteral("<table><tr><th>#</th><th>地址</th><th>模块偏移</th><th>模块</th><th>函数</th><th>源码</th></tr>");
    for (qsizetype i = 0; i < report.frames.size(); ++i) {
        const CrashStackFrame &frame = report.frames.at(i);
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
}
