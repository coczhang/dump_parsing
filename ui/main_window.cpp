#include "main_window.h"

#include "analysis_controller.h"
#include "busy_spinner.h"
#include "report_exporter.h"
#include "report_renderer.h"
#include "ui_mainwindow.h"

#include <QCheckBox>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/resources/app_icon.ico")));
    setWindowTitle(QStringLiteral("Dump 崩溃分析工具"));
    resize(980, 720);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Dump 崩溃分析工具"), this);
    auto titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    title->setFont(titleFont);
    rootLayout->addWidget(title);

    auto *formLayout = new QFormLayout;
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->addRow(QStringLiteral("Dump 文件"), createFileRow(QStringLiteral("选择 .dmp/.dump 文件"),
                                                                &dumpPathEdit,
                                                                &browseDumpButton));
    formLayout->addRow(QStringLiteral("符号文件"), createFileRow(QStringLiteral("选择 .pdb/.dbg 文件"),
                                                                &symbolPathEdit,
                                                                &browseSymbolButton));
    formLayout->addRow(QStringLiteral("源码目录"), createFileRow(QStringLiteral("可选，选择源码根目录"),
                                                                &sourceRootEdit,
                                                                &browseSourceButton));
    rootLayout->addLayout(formLayout);

    auto *exportTitle = new QLabel(QStringLiteral("3. 导出与文件"), this);
    auto exportTitleFont = exportTitle->font();
    exportTitleFont.setBold(true);
    exportTitle->setFont(exportTitleFont);
    rootLayout->addWidget(exportTitle);

    auto *exportLayout = new QFormLayout;
    exportLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    exportLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *exportDirRow = new QWidget(this);
    auto *exportDirRowLayout = new QHBoxLayout(exportDirRow);
    exportDirRowLayout->setContentsMargins(0, 0, 0, 0);
    exportDirRowLayout->setSpacing(8);

    exportDirectoryEdit = new QLineEdit(exportDirRow);
    exportDirectoryEdit->setText(QDir::toNativeSeparators(ReportExporter::defaultOutputDirectory()));
    exportDirectoryEdit->setClearButtonEnabled(true);
    exportDirRowLayout->addWidget(exportDirectoryEdit, 1);

    openExportDirButton = new QPushButton(QStringLiteral("打开目录"), exportDirRow);
    openExportDirButton->setMinimumWidth(110);
    exportDirRowLayout->addWidget(openExportDirButton);
    exportLayout->addRow(QStringLiteral("默认导出路径"), exportDirRow);

    auto *exportFormatRow = new QWidget(this);
    auto *exportFormatRowLayout = new QHBoxLayout(exportFormatRow);
    exportFormatRowLayout->setContentsMargins(0, 0, 0, 0);
    exportFormatRowLayout->setSpacing(18);

    markdownExportCheck = new QCheckBox(QStringLiteral("Markdown (.md)"), exportFormatRow);
    pdfExportCheck = new QCheckBox(QStringLiteral("PDF (.pdf)"), exportFormatRow);
    textExportCheck = new QCheckBox(QStringLiteral("TXT (.txt)"), exportFormatRow);
    markdownExportCheck->setChecked(true);
    pdfExportCheck->setChecked(false);
    textExportCheck->setChecked(false);
    exportFormatRowLayout->addWidget(markdownExportCheck);
    exportFormatRowLayout->addWidget(pdfExportCheck);
    exportFormatRowLayout->addWidget(textExportCheck);
    exportFormatRowLayout->addStretch();
    exportLayout->addRow(QStringLiteral("默认导出格式"), exportFormatRow);
    rootLayout->addLayout(exportLayout);

    auto *actionLayout = new QHBoxLayout;
    actionLayout->addStretch();

    exportButton = new QPushButton(QStringLiteral("导出报告"), this);
    exportButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    exportButton->setMinimumWidth(128);
    actionLayout->addWidget(exportButton);

    analyzeButton = new QPushButton(QStringLiteral("开始分析"), this);
    analyzeButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    analyzeButton->setMinimumWidth(128);
    actionLayout->addWidget(analyzeButton);
    rootLayout->addLayout(actionLayout);

    auto *reportTopLayout = new QHBoxLayout;
    reportTopLayout->setContentsMargins(0, 0, 0, 0);
    reportTopLayout->setSpacing(6);
    reportTopLayout->addStretch();

    reportSpinner = new BusySpinner(this);
    reportTopLayout->addWidget(reportSpinner, 0, Qt::AlignRight | Qt::AlignVCenter);
    rootLayout->addLayout(reportTopLayout);

    reportView = new QTextBrowser(this);
    reportView->setOpenExternalLinks(false);
    reportView->setMinimumHeight(480);
    reportView->setAutoFillBackground(false);
    reportView->viewport()->setAutoFillBackground(false);
    reportView->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; color: palette(window-text); border: 1px solid palette(mid); }"
        "QTextBrowser QWidget { background: transparent; }"
        "QTextBrowser { selection-background-color: palette(highlight); selection-color: palette(highlighted-text); }"));
    rootLayout->addWidget(reportView, 1);

    browseDumpButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browseSymbolButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browseSourceButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    openExportDirButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    // 分析控制器负责线程生命周期与状态机，MainWindow 只关心 UI 展示。
    analysisController = new AnalysisController(this);

    connect(browseDumpButton, &QPushButton::clicked, this, &MainWindow::browseDumpFile);
    connect(browseSymbolButton, &QPushButton::clicked, this, &MainWindow::browseSymbolFile);
    connect(browseSourceButton, &QPushButton::clicked, this, &MainWindow::browseSourceDirectory);
    connect(openExportDirButton, &QPushButton::clicked, this, &MainWindow::openExportDirectory);
    connect(analyzeButton, &QPushButton::clicked, this, &MainWindow::analyzeCrash);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportReport);
    connect(dumpPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateAnalyzeButton);
    connect(symbolPathEdit, &QLineEdit::textChanged, this, &MainWindow::updateAnalyzeButton);
    connect(markdownExportCheck, &QCheckBox::toggled, this, &MainWindow::updateExportButton);
    connect(pdfExportCheck, &QCheckBox::toggled, this, &MainWindow::updateExportButton);
    connect(textExportCheck, &QCheckBox::toggled, this, &MainWindow::updateExportButton);

    // analysisStarted: 进入忙碌态（按钮禁用 + 转圈 + 提示文案）。
    connect(analysisController, &AnalysisController::analysisStarted, this, [this]() {
        analyzeButton->setEnabled(false);
        updateExportButton();
        analyzeButton->setText(QStringLiteral("分析中..."));
        if (reportSpinner) {
            reportSpinner->start();
        }
        setReportMessage(QStringLiteral("正在调用分析脚本并解析崩溃信息，请稍候..."));
    });
    // analysisFinished: 缓存报告并刷新报告视图（HTML 渲染统一走 ReportRenderer）。
    connect(analysisController, &AnalysisController::analysisFinished, this, [this](const CrashReport &report) {
        lastReport = report;
        hasLastReport = report.ok;
        reportView->setHtml(ReportRenderer::renderHtml(report));
        updateAnalyzeButton();
        updateExportButton();
    });
    // runningChanged: 统一做“恢复可交互状态”的收口逻辑。
    connect(analysisController, &AnalysisController::runningChanged, this, [this](bool running) {
        if (!running) {
            analyzeButton->setText(QStringLiteral("开始分析"));
            if (reportSpinner) {
                reportSpinner->stop();
            }
        }
        updateAnalyzeButton();
        updateExportButton();
    });

    setReportMessage(QStringLiteral("请选择 dump 文件和匹配的 PDB/DBG 符号文件。"));
    updateAnalyzeButton();
    updateExportButton();
}

MainWindow::~MainWindow()
{
    if (reportSpinner) {
        reportSpinner->stop();
    }
    delete ui;
}

QWidget *MainWindow::createFileRow(const QString &labelText,
                                   QLineEdit **lineEdit,
                                   QPushButton **button) const
{
    auto *container = new QWidget(const_cast<MainWindow *>(this));
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    *lineEdit = new QLineEdit(container);
    (*lineEdit)->setPlaceholderText(labelText);
    (*lineEdit)->setClearButtonEnabled(true);
    layout->addWidget(*lineEdit, 1);

    *button = new QPushButton(QStringLiteral("浏览"), container);
    (*button)->setMinimumWidth(92);
    layout->addWidget(*button);

    return container;
}

void MainWindow::browseDumpFile()
{
    const QString initialDir = QFileInfo(dumpPathEdit->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 Dump 文件"),
        initialDir,
        QStringLiteral("Dump 文件 (*.dmp *.dump);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        dumpPathEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::browseSymbolFile()
{
    const QString initialDir = QFileInfo(symbolPathEdit->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 PDB/DBG 符号文件"),
        initialDir,
        QStringLiteral("符号文件 (*.pdb *.dbg);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        symbolPathEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::browseSourceDirectory()
{
    const QString current = sourceRootEdit->text().trimmed();
    const QString initialDir = current.isEmpty() ? QDir::currentPath() : current;
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择源码根目录"),
        initialDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty()) {
        sourceRootEdit->setText(QDir::toNativeSeparators(path));
    }
}

void MainWindow::analyzeCrash()
{
    if (!analysisController || analysisController->isRunning()) {
        return;
    }

    // UI 层只做参数采集，不做任何耗时分析逻辑。
    // 这样可以保证窗口拖动、重绘和输入响应不被阻塞。
    const QString dumpPath = dumpPathEdit->text().trimmed();
    const QString symbolPath = symbolPathEdit->text().trimmed();
    const QString sourceRoot = sourceRootEdit->text().trimmed();
    analysisController->startAnalysis(dumpPath, symbolPath, sourceRoot);
    return;

#if 0

    analyzeButton->setEnabled(false);
    updateExportButton();
    analyzeButton->setText(QStringLiteral("分析中..."));
    if (reportSpinner) {
        reportSpinner->start();
    }
    setReportMessage(QStringLiteral("正在调用分析脚本并解析崩溃信息，请稍候..."));

    const QString dumpPath = dumpPathEdit->text().trimmed();
    const QString symbolPath = symbolPathEdit->text().trimmed();
    const QString sourceRoot = sourceRootEdit->text().trimmed();
    analysisController->startAnalysis(dumpPath, symbolPath, sourceRoot);

#if 0

    QPointer<MainWindow> self(this);
    analysisThread = QThread::create([self, dumpPath, symbolPath, sourceRoot]() {
        CrashAnalyzer analyzerWorker;
        const CrashReport report = analyzerWorker.analyze(dumpPath, symbolPath, sourceRoot);

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(
            self,
            [self, report]() {
                if (!self) {
                    return;
                }
                self->lastReport = report;
                self->hasLastReport = report.ok;
                self->reportView->setHtml(report.toHtml());
                if (self->reportSpinner) {
                    self->reportSpinner->stop();
                }
                self->updateAnalyzeButton();
                self->updateExportButton();
            },
            Qt::QueuedConnection);
    });

    connect(analysisThread, &QThread::finished, this, [this]() {
        if (reportSpinner) {
            reportSpinner->stop();
        }
        analyzeButton->setText(QStringLiteral("开始分析"));
        analysisThread = nullptr;
        updateAnalyzeButton();
        updateExportButton();
    });
    connect(analysisThread, &QThread::finished, analysisThread, &QObject::deleteLater);
    analysisThread->start();
#endif
#endif
}

void MainWindow::updateAnalyzeButton()
{
    const bool ready = QFileInfo::exists(dumpPathEdit->text().trimmed())
        && QFileInfo::exists(symbolPathEdit->text().trimmed());
    const bool running = analysisController && analysisController->isRunning();
    analyzeButton->setEnabled(ready && !running);
}

void MainWindow::setReportMessage(const QString &message)
{
    const QPalette palette = reportView->palette();
    reportView->setHtml(QStringLiteral("<html><body style=\"font-family:'Microsoft YaHei','Segoe UI',sans-serif;"
                                       "font-size:13px;background:transparent;color:%1;\">%2</body></html>")
                            .arg(colorName(palette.color(QPalette::WindowText)),
                                 message.toHtmlEscaped()));
}

void MainWindow::openExportDirectory()
{
    QString directoryPath = exportDirectoryEdit ? exportDirectoryEdit->text().trimmed() : QString();
    if (directoryPath.isEmpty()) {
        directoryPath = ReportExporter::defaultOutputDirectory();
        if (exportDirectoryEdit) {
            exportDirectoryEdit->setText(QDir::toNativeSeparators(directoryPath));
        }
    }

    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        QMessageBox::warning(this,
                             QStringLiteral("导出目录"),
                             QStringLiteral("无法创建导出目录：%1")
                                 .arg(QDir::toNativeSeparators(directoryPath)));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
}

void MainWindow::exportReport()
{
    ReportExportOptions options;
    options.outputDirectory = exportDirectoryEdit ? exportDirectoryEdit->text().trimmed() : QString();
    options.exportMarkdown = markdownExportCheck && markdownExportCheck->isChecked();
    options.exportPdf = pdfExportCheck && pdfExportCheck->isChecked();
    options.exportText = textExportCheck && textExportCheck->isChecked();

    if (options.outputDirectory.isEmpty() && exportDirectoryEdit) {
        options.outputDirectory = ReportExporter::defaultOutputDirectory();
        exportDirectoryEdit->setText(QDir::toNativeSeparators(options.outputDirectory));
    }

    const ReportExportResult result = ReportExporter::exportReport(lastReport, options);
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("导出报告"), result.errors.join(QLatin1Char('\n')));
        return;
    }

    QString message = QStringLiteral("导出完成：\n%1").arg(result.exportedFiles.join(QLatin1Char('\n')));
    if (!result.errors.isEmpty()) {
        message += QStringLiteral("\n\n部分格式导出失败：\n%1").arg(result.errors.join(QLatin1Char('\n')));
    }
    QMessageBox::information(this, QStringLiteral("导出报告"), message);
}

void MainWindow::updateExportButton()
{
    if (!exportButton) {
        return;
    }

    const bool hasFormat = (markdownExportCheck && markdownExportCheck->isChecked())
        || (pdfExportCheck && pdfExportCheck->isChecked())
        || (textExportCheck && textExportCheck->isChecked());
    const bool running = analysisController && analysisController->isRunning();
    exportButton->setEnabled(hasLastReport && hasFormat && !running);
}
